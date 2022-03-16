/* Copyright 2021 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <errno.h>
#include <stdint.h>
#include <sys/ioctl.h>
#include <sys/param.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/time.h>
#include <syslog.h>
#include <time.h>

#include "audio_thread.h"
#include "audio_thread_log.h"
#include "byte_buffer.h"
#include "cras_iodev_list.h"
#include "cras_a2dp_manager.h"
#include "cras_audio_area.h"
#include "cras_audio_thread_monitor.h"
#include "cras_hfp_manager.h"
#include "cras_iodev.h"
#include "cras_types.h"
#include "cras_string.h"
#include "cras_util.h"
#include "sfh.h"
#include "utlist.h"

#define PCM_BUF_MAX_SIZE_FRAMES (4096 * 4)

/* Floss currently set a 10ms poll interval as A2DP_DATA_READ_POLL_MS.
 * Double it and use for scheduling here. */
#define PCM_BLOCK_MS 20

/* 8000 (sampling rate) * 10ms * 2 (S16_LE)
 * 10ms equivalent of PCM data for HFP narrow band. This static value is a
 * temporary solution and should be refined to a better scheduling strategy. */
#define HFP_PACKET_SIZE 160

/* Schedule the first delay sync 500ms after stream starts, and redo
 * every 10 seconds. */
#define INIT_DELAY_SYNC_MSEC 500
#define DELAY_SYNC_PERIOD_MSEC 10000

/* There's a period of time after streaming starts before BT stack
 * is able to provide non-zero data_position_ts. During this period
 * use a default value for the delay which is supposed to be derived
 * from data_position_ts. */
#define DEFAULT_BT_STACK_DELAY_SEC 0.2f

/* Threshold for reasonable a2dp throttle log in audio dump. */
static const struct timespec throttle_log_threshold = {
	0, 10000000 /* 10ms */
};

/* Threshold for severe a2dp throttle event. */
static const struct timespec throttle_event_threshold = {
	2, 0 /* 2s */
};

/* The max buffer size. Note that the actual used size must set to multiple
 * of SCO packet size, and the packet size does not necessarily be equal to
 * MTU. We should keep this as common multiple of possible packet sizes, for
 * example: 48, 60, 64, 128.
 */
#define FLOSS_HFP_MAX_BUF_SIZE_BYTES 28800

/* Child of cras_iodev to handle bluetooth A2DP streaming.
 * Members:
 *    base - The cras_iodev structure "base class"
 *    audio_fd - The sockets for device to read and write
 *    ncm_buf - Buffer to hold pcm samples before encode.
 *    next_flush_time - The time when it is okay for next flush call.
 *    flush_period - The time period between two a2dp packet writes.
 *    write_block - How many frames of audio samples we prefer to write in one
 *        socket write.
 *    total_written_bytes - Stores the total audio data in bytes written to BT.
 *    last_write_ts - The timestamp of when last audio data was written to BT.
 *    bt_stack_delay - The calculated delay in frames from
 *        a2dp_pcm_update_bt_stack_delay.
 *    a2dp - The associated cras_a2dp object.
 *    hfp - The associated cras_hfp object.
 *    started - If the device has been configured and attached with any stream.
 */
struct fl_pcm_io {
	struct cras_iodev base;
	int audio_fd;
	struct byte_buffer *pcm_buf;
	struct timespec next_flush_time;
	struct timespec flush_period;
	unsigned int write_block;
	unsigned long total_written_bytes;
	struct timespec last_write_ts;
	unsigned int bt_stack_delay;
	struct cras_a2dp *a2dp;
	struct cras_hfp *hfp;
	int started;
};

static int flush(const struct cras_iodev *iodev);

static int update_supported_formats(struct cras_iodev *iodev)
{
	/* Supported formats are fixed when iodev created. */
	/* TODO(b/214148074): Support WBS */
	return 0;
}

static unsigned int bt_local_queued_frames(const struct cras_iodev *iodev)
{
	struct fl_pcm_io *pcmio = (struct fl_pcm_io *)iodev;
	return buf_queued(pcmio->pcm_buf) /
	       cras_get_format_bytes(iodev->format);
}

static int frames_queued(const struct cras_iodev *iodev,
			 struct timespec *tstamp)
{
	clock_gettime(CLOCK_MONOTONIC_RAW, tstamp);
	return bt_local_queued_frames(iodev);
}

/*
 * Utility function to fill zero frames until buffer level reaches
 * target_level. This is useful to allocate just enough data to write
 * to controller, while not introducing extra latency.
 */
static int fill_zeros_to_target_level(struct cras_iodev *iodev,
				      unsigned int target_level)
{
	unsigned int local_queued_frames = bt_local_queued_frames(iodev);

	if (local_queued_frames < target_level)
		return cras_iodev_fill_odev_zeros(
			iodev, target_level - local_queued_frames);
	return 0;
}

/*
 * dev_io_playback_write() has the logic to detect underrun scenario
 * and calls into this underrun ops, by comparing buffer level with
 * number of frames just written. Note that it's not correct 100% of
 * the time in a2dp case, because we lose track of samples once they're
 * flushed to socket.
 */
static int output_underrun(struct cras_iodev *iodev)
{
	return 0;
}

/*
 * This will be called multiple times when a2dpio is in no_stream state
 * frames_to_play_in_sleep ops determins how regular this will be called.
 */
static int enter_no_stream(struct fl_pcm_io *a2dpio)
{
	struct cras_iodev *odev = &a2dpio->base;
	int rc;

	/* We want hw_level to stay bewteen 1-2 times of write_block. */
	rc = fill_zeros_to_target_level(odev, 2 * a2dpio->write_block);
	if (rc)
		syslog(LOG_ERR, "Error in A2DP enter_no_stream");
	return flush(odev);
}

/*
 * This is called when stream data is available to write. Prepare audio
 * data to one min_buffer_level. Don't flush it now because stream data is
 * coming right up which will trigger next flush at appropriate time.
 */
static int leave_no_stream(struct fl_pcm_io *a2dpio)
{
	struct cras_iodev *odev = &a2dpio->base;

	/*
	 * Since stream data is ready, just make sure hw_level doesn't underrun
	 * after one flush. Hence setting the target level to write_block.
         */
	return fill_zeros_to_target_level(odev, a2dpio->write_block);
}

/*
 * Makes sure there's enough data(zero frames) to flush when no stream presents.
 * Note that the underrun condition is when real buffer level goes below
 * min_buffer_level, so we want to keep data at a reasonable higher level on top
 * of that.
 */
static int a2dp_no_stream(struct cras_iodev *odev, int enable)
{
	struct fl_pcm_io *pcmio = (struct fl_pcm_io *)odev;

	if (enable)
		return enter_no_stream(pcmio);
	else
		return leave_no_stream(pcmio);
}

static int hfp_no_stream(struct cras_iodev *iodev, int enable)
{
	struct fl_pcm_io *hfpio = (struct fl_pcm_io *)iodev;

	if (iodev->direction != CRAS_STREAM_OUTPUT)
		return 0;

	/* Have output fallback to sending zeros to HF. */
	if (enable) {
		hfpio->started = 0;
		memset(hfpio->pcm_buf->bytes, 0, hfpio->pcm_buf->used_size);
	} else {
		hfpio->started = 1;
	}
	return 0;
}

static int hfp_is_free_running(const struct cras_iodev *iodev)
{
	struct fl_pcm_io *hfpio = (struct fl_pcm_io *)iodev;

	if (iodev->direction != CRAS_STREAM_OUTPUT)
		return 0;

	/* If NOT started, hfp_wrtie will automatically puts more data to
	 * socket so audio thread doesn't need to wake up for us. */
	return !hfpio->started;
}

/*
 * To be called when PCM socket becomes writable.
 */
static int a2dp_socket_write_cb(void *arg, int revent)
{
	struct cras_iodev *iodev = (struct cras_iodev *)arg;
	return flush(iodev);
}

static int a2dp_configure_dev(struct cras_iodev *iodev)
{
	struct fl_pcm_io *a2dpio = (struct fl_pcm_io *)iodev;
	int rc;
	size_t format_bytes;

	rc = cras_floss_a2dp_start(a2dpio->a2dp, iodev->format,
				   &a2dpio->audio_fd);
	if (rc < 0) {
		syslog(LOG_ERR, "A2dp start failed");
		return rc;
	}

	/* Assert format is set before opening device. */
	if (iodev->format == NULL)
		return -EINVAL;
	iodev->format->format = SND_PCM_FORMAT_S16_LE;
	format_bytes = cras_get_format_bytes(iodev->format);
	cras_iodev_init_audio_area(iodev, iodev->format->num_channels);

	a2dpio->total_written_bytes = 0;
	a2dpio->bt_stack_delay = 0;

	/* Configure write_block to frames equivalent to PCM_BLOCK_MS.
	 * And make buffer_size integer multiple of write_block so we
	 * don't get cut easily in ring buffer. */
	a2dpio->write_block = iodev->format->frame_rate * PCM_BLOCK_MS / 1000;
	iodev->buffer_size = PCM_BUF_MAX_SIZE_FRAMES / a2dpio->write_block *
			     a2dpio->write_block;

	a2dpio->pcm_buf = byte_buffer_create(iodev->buffer_size * format_bytes);
	if (!a2dpio->pcm_buf)
		return -ENOMEM;

	/* Initialize flush_period by write_block, it will be changed
	 * later based on socket write schedule. */
	cras_frames_to_time(a2dpio->write_block, iodev->format->frame_rate,
			    &a2dpio->flush_period);

	/*
	 * As we directly write pcm here, there is no min buffer limitation.
	 */
	iodev->min_buffer_level = 0;

	audio_thread_add_events_callback(a2dpio->audio_fd, a2dp_socket_write_cb,
					 iodev, POLLOUT | POLLERR | POLLHUP);
	audio_thread_config_events_callback(a2dpio->audio_fd, TRIGGER_NONE);
	return 0;
}

static int hfp_read(const struct cras_iodev *iodev)
{
	int fd, rc;
	uint8_t *buf;
	unsigned int to_read;
	size_t packet_size;
	struct fl_pcm_io *idev = (struct fl_pcm_io *)iodev;

	packet_size = HFP_PACKET_SIZE;
	fd = cras_floss_hfp_get_fd(idev->hfp);
do_recv:
	buf = buf_write_pointer_size(idev->pcm_buf, &to_read);

	if (to_read > packet_size)
		to_read = packet_size;

	rc = recv(fd, buf, to_read, MSG_DONTWAIT);
	if (rc <= 0) {
		if (rc < 0 && errno != EWOULDBLOCK && errno != EAGAIN) {
			syslog(LOG_ERR, "Recv error %s", cras_strerror(errno));
			return -1;
		}
		return 0;
	}
	buf_increment_write(idev->pcm_buf, rc);

	/* Ignore the bytes just read if input dev not in present */
	if (!idev->started)
		buf_increment_read(idev->pcm_buf, rc);

	/* Try to receive more if we haven't received the amount of data we
	 * prefer. */
	packet_size -= rc;
	if (packet_size > 0)
		goto do_recv;

	return 0;
}

static int hfp_write(const struct cras_iodev *iodev)
{
	int fd, rc;
	uint8_t *buf;
	unsigned int to_send;
	size_t packet_size;
	struct fl_pcm_io *odev = (struct fl_pcm_io *)iodev;

	packet_size = HFP_PACKET_SIZE;
	/* Without output stream's presence, we shall still send zero packets
	 * to HF. This is required for some HF devices to start sending non-zero
	 * data to AG.
	 */
	if (!odev->started)
		buf_increment_write(odev->pcm_buf, packet_size);

	buf = buf_read_pointer_size(odev->pcm_buf, &to_send);
	if (to_send < packet_size)
		return 0;

	fd = cras_floss_hfp_get_fd(odev->hfp);
do_send:
	rc = send(fd, buf, packet_size, MSG_DONTWAIT);
	if (rc <= 0) {
		if (rc < 0 && errno != EWOULDBLOCK && errno != EAGAIN) {
			syslog(LOG_ERR, "Send error %s", cras_strerror(errno));
			return -1;
		}
		return 0;
	}
	buf_increment_read(odev->pcm_buf, rc);

	/* Keep trying to write more if we haven't sent the amount of data
	 * we tend to. */
	packet_size -= rc;
	if (packet_size > 0)
		goto do_send;

	return 0;
}

static int hfp_socket_read_write_cb(void *arg, int revents)
{
	int rc;
	struct cras_hfp *hfp = (struct cras_hfp *)arg;
	struct cras_iodev *idev, *odev;

	cras_floss_hfp_get_iodevs(hfp, &idev, &odev);

	/* Allow last read before handling error or hang-up events. */
	if (revents & POLLIN) {
		rc = hfp_read(idev);
		if (rc)
			return rc;
	}
	if (revents & (POLLERR | POLLHUP)) {
		syslog(LOG_ERR, "Error polling SCO socket, revents %d",
		       revents);
		return -1;
	}

	return hfp_write(odev);
}

static int hfp_configure_dev(struct cras_iodev *iodev)
{
	struct fl_pcm_io *hfpio = (struct fl_pcm_io *)iodev;
	int rc;

	/* Assert format is set before opening device. */
	if (iodev->format == NULL)
		return -EINVAL;
	iodev->format->format = SND_PCM_FORMAT_S16_LE;
	cras_iodev_init_audio_area(iodev, iodev->format->num_channels);

	buf_reset(hfpio->pcm_buf);
	iodev->buffer_size = hfpio->pcm_buf->used_size /
			     cras_get_format_bytes(iodev->format);

	hfpio->bt_stack_delay = 0;

	/* As we directly write PCM here, there is no min buffer limitation. */
	iodev->min_buffer_level = 0;

	rc = cras_floss_hfp_start(hfpio->hfp, hfp_socket_read_write_cb,
				  iodev->direction);
	if (rc < 0) {
		syslog(LOG_ERR, "HFP failed to start");
		return rc;
	}

	hfpio->started = 1;
	return 0;
}

static int a2dp_start(const struct cras_iodev *iodev)
{
	struct fl_pcm_io *a2dpio = (struct fl_pcm_io *)iodev;

	/*
	 * This is called when iodev in open state, at the moment when
	 * output sample is ready. Initialize the next_flush_time for
	 * following flush calls.
	 */
	clock_gettime(CLOCK_MONOTONIC_RAW, &a2dpio->next_flush_time);
	cras_floss_a2dp_delay_sync(a2dpio->a2dp, INIT_DELAY_SYNC_MSEC,
				   DELAY_SYNC_PERIOD_MSEC);

	return 0;
}

static int a2dp_close_dev(struct cras_iodev *iodev)
{
	int err;
	struct fl_pcm_io *a2dpio = (struct fl_pcm_io *)iodev;

	audio_thread_rm_callback_sync(cras_iodev_list_get_audio_thread(),
				      a2dpio->audio_fd);
	close(a2dpio->audio_fd);
	err = cras_floss_a2dp_stop(a2dpio->a2dp);
	if (err < 0)
		syslog(LOG_ERR, "Socket release failed");

	cras_a2dp_cancel_suspend();
	byte_buffer_destroy(&a2dpio->pcm_buf);
	cras_iodev_free_format(iodev);
	cras_iodev_free_audio_area(iodev);
	return 0;
}

static int hfp_close_dev(struct cras_iodev *iodev)
{
	struct fl_pcm_io *hfpio = (struct fl_pcm_io *)iodev;

	hfpio->started = 0;
	cras_floss_hfp_stop(hfpio->hfp, iodev->direction);

	if (iodev->direction == CRAS_STREAM_OUTPUT)
		memset(hfpio->pcm_buf->bytes, 0, hfpio->pcm_buf->used_size);

	cras_iodev_free_format(iodev);
	cras_iodev_free_audio_area(iodev);
	return 0;
}

static unsigned int a2dp_frames_to_play_in_sleep(struct cras_iodev *iodev,
						 unsigned int *hw_level,
						 struct timespec *hw_tstamp)
{
	struct fl_pcm_io *a2dpio = (struct fl_pcm_io *)iodev;
	int frames_until;

	*hw_level = frames_queued(iodev, hw_tstamp);

	frames_until = cras_frames_until_time(&a2dpio->next_flush_time,
					      iodev->format->frame_rate);
	if (frames_until > 0)
		return frames_until;

	/* If time has passed next_flush_time, for example when socket write
	 * throttles, sleep a moderate of time so that audio thread doesn't
	 * busy wake up. */
	return a2dpio->write_block;
}

/* Flush PCM data to the socket.
 * Returns:
 *    0 when the flush succeeded, -1 when error occurred.
 */
static int flush(const struct cras_iodev *iodev)
{
	int written = 0;
	unsigned int queued_frames;
	size_t format_bytes;
	struct fl_pcm_io *a2dpio;
	struct timespec now, ts;
	static const struct timespec flush_wake_fuzz_ts = {
		0, 1000000 /* 1ms */
	};

	a2dpio = (struct fl_pcm_io *)iodev;

	ATLOG(atlog, AUDIO_THREAD_A2DP_FLUSH, iodev->state,
	      a2dpio->next_flush_time.tv_sec, a2dpio->next_flush_time.tv_nsec);
	/* Only allow data to be flushed after start() ops is called. */
	if ((iodev->state != CRAS_IODEV_STATE_NORMAL_RUN) &&
	    (iodev->state != CRAS_IODEV_STATE_NO_STREAM_RUN))
		return 0;

do_flush:
	/* If flush gets called before targeted next flush time, do nothing. */
	clock_gettime(CLOCK_MONOTONIC_RAW, &now);
	add_timespecs(&now, &flush_wake_fuzz_ts);
	if (!timespec_after(&now, &a2dpio->next_flush_time)) {
		if (iodev->buffer_size == bt_local_queued_frames(iodev)) {
			/*
			 * If buffer is full, audio thread will no longer call
			 * into get/put buffer in subsequent wake-ups. In that
			 * case set the registered callback to be triggered at
			 * next audio thread wake up.
			 */
			audio_thread_config_events_callback(a2dpio->audio_fd,
							    TRIGGER_WAKEUP);
			cras_audio_thread_event_a2dp_overrun();
			syslog(LOG_WARNING, "Buffer overrun in A2DP iodev");
		}
		return 0;
	}
	/* If the A2DP write schedule miss exceeds a small threshold, log it for
	 * debug purpose. */
	subtract_timespecs(&now, &a2dpio->next_flush_time, &ts);
	if (timespec_after(&ts, &throttle_log_threshold))
		ATLOG(atlog, AUDIO_THREAD_A2DP_THROTTLE_TIME, ts.tv_sec,
		      ts.tv_nsec, bt_local_queued_frames(iodev));

	/* Log an event if the A2DP write schedule miss exceeds a large
	 * threshold that we consider it as something severe. */
	if (timespec_after(&ts, &throttle_event_threshold))
		cras_audio_thread_event_a2dp_throttle();

	format_bytes = cras_get_format_bytes(iodev->format);
	if (bt_local_queued_frames(iodev) >= a2dpio->write_block) {
		written = send(a2dpio->audio_fd,
			       buf_read_pointer(a2dpio->pcm_buf),
			       MIN(a2dpio->write_block * format_bytes,
				   buf_readable(a2dpio->pcm_buf)),
			       MSG_DONTWAIT);
	}

	ATLOG(atlog, AUDIO_THREAD_A2DP_WRITE, written / format_bytes,
	      buf_readable(a2dpio->pcm_buf), 0);

	if (written < 0) {
		if (errno == EAGAIN) {
			/* If EAGAIN error lasts longer than 5 seconds, suspend
			 * the a2dp connection. */
			cras_a2dp_schedule_suspend(5000);
			audio_thread_config_events_callback(a2dpio->audio_fd,
							    TRIGGER_WAKEUP);
			return 0;
		} else {
			cras_a2dp_cancel_suspend();
			cras_a2dp_schedule_suspend(0);

			audio_thread_config_events_callback(a2dpio->audio_fd,
							    TRIGGER_NONE);
			return written;
		}
	}

	if (written > 0) {
		/* Adds some time to next_flush_time according to how many
		 * frames just written to socket. */
		cras_frames_to_time(written / format_bytes,
				    iodev->format->frame_rate,
				    &a2dpio->flush_period);
		add_timespecs(&a2dpio->next_flush_time, &a2dpio->flush_period);
		buf_increment_read(a2dpio->pcm_buf, written);
		a2dpio->total_written_bytes += written;
		a2dpio->last_write_ts = now;
	}

	/* a2dp_write no longer return -EAGAIN when reaches here, disable
	 * the polling write callback. */
	audio_thread_config_events_callback(a2dpio->audio_fd, TRIGGER_NONE);

	cras_a2dp_cancel_suspend();

	/* If it looks okay to write more and we do have queued data, try
	 * to write more.
	 */
	queued_frames = buf_queued(a2dpio->pcm_buf) / format_bytes;
	if (written && (queued_frames > a2dpio->write_block))
		goto do_flush;

	return 0;
}

static int delay_frames(const struct cras_iodev *iodev)
{
	const struct fl_pcm_io *pcmio = (struct fl_pcm_io *)iodev;
	struct timespec tstamp;

	/* The number of frames in the pcm buffer plus the delay
	 * derived from a2dp_pcm_update_bt_stack_delay. */
	return frames_queued(iodev, &tstamp) + pcmio->bt_stack_delay;
}

static int get_buffer(struct cras_iodev *iodev, struct cras_audio_area **area,
		      unsigned *frames)
{
	struct fl_pcm_io *pcmio;
	uint8_t *dst = NULL;
	unsigned buf_avail = 0;
	size_t format_bytes;

	pcmio = (struct fl_pcm_io *)iodev;

	if (iodev->direction == CRAS_STREAM_OUTPUT && iodev->format) {
		dst = buf_write_pointer_size(pcmio->pcm_buf, &buf_avail);
	} else if (iodev->direction == CRAS_STREAM_INPUT && iodev->format) {
		dst = buf_read_pointer_size(pcmio->pcm_buf, &buf_avail);
	} else {
		*frames = 0;
		return 0;
	}

	format_bytes = cras_get_format_bytes(iodev->format);

	*frames = MIN(*frames, buf_writable(pcmio->pcm_buf) / format_bytes);
	iodev->area->frames = *frames;
	cras_audio_area_config_buf_pointers(iodev->area, iodev->format, dst);

	*area = iodev->area;
	return 0;
}

static int a2dp_put_buffer(struct cras_iodev *iodev, unsigned nwritten)
{
	size_t written_bytes;
	size_t format_bytes;
	struct fl_pcm_io *a2dpio = (struct fl_pcm_io *)iodev;

	format_bytes = cras_get_format_bytes(iodev->format);
	written_bytes = nwritten * format_bytes;

	if (written_bytes > buf_writable(a2dpio->pcm_buf))
		return -EINVAL;

	buf_increment_write(a2dpio->pcm_buf, written_bytes);

	return flush(iodev);
}

static int hfp_put_buffer(struct cras_iodev *iodev, unsigned frames)
{
	struct fl_pcm_io *pcmio = (struct fl_pcm_io *)iodev;
	size_t format_bytes;

	if (!frames)
		return 0;

	format_bytes = cras_get_format_bytes(iodev->format);

	if (iodev->direction == CRAS_STREAM_OUTPUT) {
		buf_increment_write(pcmio->pcm_buf, frames * format_bytes);
	} else if (iodev->direction == CRAS_STREAM_INPUT) {
		buf_increment_read(pcmio->pcm_buf, frames * format_bytes);
	}

	return 0;
}

static int a2dp_flush_buffer(struct cras_iodev *iodev)
{
	return 0;
}

static int hfp_flush_buffer(struct cras_iodev *iodev)
{
	struct fl_pcm_io *pcmio = (struct fl_pcm_io *)iodev;
	size_t format_bytes;
	unsigned nframes;

	format_bytes = cras_get_format_bytes(iodev->format);
	if (iodev->direction == CRAS_STREAM_INPUT) {
		nframes = buf_queued(pcmio->pcm_buf) / format_bytes;
		buf_increment_read(pcmio->pcm_buf, nframes * format_bytes);
	}
	return 0;
}

static void a2dp_set_volume(struct cras_iodev *iodev)
{
	struct fl_pcm_io *a2dpio = (struct fl_pcm_io *)iodev;

	cras_floss_a2dp_set_volume(a2dpio->a2dp, iodev->active_node->volume);
}

static void hfp_set_volume(struct cras_iodev *iodev)
{
	/* TODO(b/215089433): Support VGS. */
}

static void update_active_node(struct cras_iodev *iodev, unsigned node_idx,
			       unsigned dev_enabled)
{
}

void pcm_free_base_resources(struct fl_pcm_io *pcmio)
{
	struct cras_ionode *node;

	node = pcmio->base.active_node;
	if (node) {
		cras_iodev_rm_node(&pcmio->base, node);
		free(node);
	}
	free(pcmio->base.supported_channel_counts);
	free(pcmio->base.supported_rates);
	free(pcmio->base.supported_formats);
}

struct cras_iodev *a2dp_pcm_iodev_create(struct cras_a2dp *a2dp,
					 int sample_rate, int bits_per_sample,
					 int channel_mode)
{
	int err;
	struct fl_pcm_io *a2dpio;
	struct cras_iodev *iodev;
	struct cras_ionode *node;
	const char *addr, *name;

	a2dpio = (struct fl_pcm_io *)calloc(1, sizeof(*a2dpio));
	if (!a2dpio)
		goto error;

	iodev = &a2dpio->base;

	a2dpio->audio_fd = -1;
	a2dpio->a2dp = a2dp;

	/* A2DP only does output now */
	iodev->direction = CRAS_STREAM_OUTPUT;

	name = cras_floss_a2dp_get_display_name(a2dp);
	snprintf(iodev->info.name, sizeof(iodev->info.name), "[A2DP]%s", name);
	iodev->info.name[ARRAY_SIZE(iodev->info.name) - 1] = '\0';

	/* Address determines the unique stable id. */
	addr = cras_floss_a2dp_get_addr(a2dp);
	iodev->info.stable_id = SuperFastHash(addr, strlen(addr), strlen(addr));

	iodev->configure_dev = a2dp_configure_dev;
	iodev->frames_queued = frames_queued;
	iodev->delay_frames = delay_frames;
	iodev->get_buffer = get_buffer;
	iodev->put_buffer = a2dp_put_buffer;
	iodev->flush_buffer = a2dp_flush_buffer;
	iodev->no_stream = a2dp_no_stream;
	iodev->output_underrun = output_underrun;
	iodev->close_dev = a2dp_close_dev;
	iodev->update_supported_formats = update_supported_formats;
	iodev->update_active_node = update_active_node;
	iodev->set_volume = a2dp_set_volume;
	iodev->start = a2dp_start;
	iodev->frames_to_play_in_sleep = a2dp_frames_to_play_in_sleep;

	cras_floss_a2dp_fill_format(sample_rate, bits_per_sample, channel_mode,
				    &iodev->supported_rates,
				    &iodev->supported_formats,
				    &iodev->supported_channel_counts);

	/* Create an empty ionode */
	node = (struct cras_ionode *)calloc(1, sizeof(*node));
	node->dev = iodev;
	strcpy(node->name, iodev->info.name);
	node->plugged = 1;
	node->type = CRAS_NODE_TYPE_BLUETOOTH;
	node->volume = 100;
	gettimeofday(&node->plugged_time, NULL);

	cras_iodev_add_node(iodev, node);
	err = cras_iodev_list_add_output(iodev);
	if (err)
		goto error;

	cras_iodev_set_active_node(iodev, node);

	ewma_power_disable(&iodev->ewma);

	return iodev;
error:
	if (a2dpio) {
		pcm_free_base_resources(a2dpio);
		free(a2dpio);
	}
	return NULL;
}

void a2dp_pcm_iodev_destroy(struct cras_iodev *iodev)
{
	struct fl_pcm_io *a2dpio = (struct fl_pcm_io *)iodev;

	/* Free resources when device successfully removed. */
	pcm_free_base_resources(a2dpio);
	cras_iodev_list_rm_output(iodev);
	cras_iodev_free_resources(iodev);
	free(a2dpio);
}

void a2dp_pcm_update_volume(struct cras_iodev *iodev, unsigned int volume)
{
	if (!iodev->active_node)
		return;

	iodev->active_node->volume = volume;
	cras_iodev_list_notify_node_volume(iodev->active_node);
}

void a2dp_pcm_update_bt_stack_delay(struct cras_iodev *iodev,
				    uint64_t remote_delay_report_ns,
				    uint64_t total_bytes_read,
				    struct timespec *data_position_ts)
{
	struct fl_pcm_io *a2dpio = (struct fl_pcm_io *)iodev;
	size_t format_bytes = cras_get_format_bytes(iodev->format);
	struct timespec diff;
	unsigned int delay;

	/* The BT stack delay is composed by two parts: the delay from remote
	 * headset, and the delay from local BT stack. */
	diff.tv_sec = 0;
	diff.tv_nsec = remote_delay_report_ns;
	while (diff.tv_nsec >= 1000000000) {
		diff.tv_nsec -= 1000000000;
		diff.tv_sec += 1;
	}
	delay = cras_time_to_frames(&diff, iodev->format->frame_rate);

	/* Local BT stack delay is calculated based on the formula
	 * (N1 - N0) + rate * (T1 - T0). */
	if (!data_position_ts->tv_sec && !data_position_ts->tv_nsec) {
		delay += iodev->format->frame_rate * DEFAULT_BT_STACK_DELAY_SEC;
	} else if (timespec_after(data_position_ts, &a2dpio->last_write_ts)) {
		subtract_timespecs(data_position_ts, &a2dpio->last_write_ts,
				   &diff);
		delay += (a2dpio->total_written_bytes - total_bytes_read) /
				 format_bytes +
			 cras_time_to_frames(&diff, iodev->format->frame_rate);
	} else {
		subtract_timespecs(&a2dpio->last_write_ts, data_position_ts,
				   &diff);
		delay += (a2dpio->total_written_bytes - total_bytes_read) /
				 format_bytes -
			 cras_time_to_frames(&diff, iodev->format->frame_rate);
	}
	a2dpio->bt_stack_delay = delay;

	syslog(LOG_DEBUG, "Update: bt_stack_delay %u", a2dpio->bt_stack_delay);
}

struct cras_iodev *hfp_pcm_iodev_create(struct cras_hfp *hfp,
					enum CRAS_STREAM_DIRECTION dir)
{
	int err;
	struct fl_pcm_io *hfpio;
	struct cras_iodev *iodev;
	struct cras_ionode *node;
	const char *addr, *name;

	hfpio = (struct fl_pcm_io *)calloc(1, sizeof(*hfpio));
	if (!hfpio)
		goto error;

	iodev = &hfpio->base;

	hfpio->started = 0;
	hfpio->hfp = hfp;

	iodev->direction = dir;

	name = cras_floss_hfp_get_display_name(hfp);
	snprintf(iodev->info.name, sizeof(iodev->info.name), "[HFP]%s", name);
	iodev->info.name[ARRAY_SIZE(iodev->info.name) - 1] = '\0';

	/* Address determines the unique stable id. */
	addr = cras_floss_hfp_get_addr(hfp);
	iodev->info.stable_id = SuperFastHash(addr, strlen(addr), strlen(addr));

	iodev->configure_dev = hfp_configure_dev;
	iodev->frames_queued = frames_queued;
	iodev->delay_frames = delay_frames;
	iodev->get_buffer = get_buffer;
	iodev->put_buffer = hfp_put_buffer;
	iodev->flush_buffer = hfp_flush_buffer;
	iodev->no_stream = hfp_no_stream;
	iodev->close_dev = hfp_close_dev;
	iodev->update_supported_formats = update_supported_formats;
	iodev->update_active_node = update_active_node;
	iodev->set_volume = hfp_set_volume;
	iodev->output_underrun = output_underrun;
	iodev->is_free_running = hfp_is_free_running;

	err = cras_floss_hfp_fill_format(hfp, &iodev->supported_rates,
					 &iodev->supported_formats,
					 &iodev->supported_channel_counts);
	if (err)
		goto error;

	/* Record max supported channels into cras_iodev_info. */
	iodev->info.max_supported_channels = 1;

	/* Create an empty ionode */
	node = (struct cras_ionode *)calloc(1, sizeof(*node));
	node->dev = iodev;
	strcpy(node->name, iodev->info.name);

	node->plugged = 1;
	node->type = CRAS_NODE_TYPE_BLUETOOTH;

	node->volume = 100;
	gettimeofday(&node->plugged_time, NULL);

	hfpio->pcm_buf = byte_buffer_create(FLOSS_HFP_MAX_BUF_SIZE_BYTES);
	if (!hfpio->pcm_buf)
		goto error;

	cras_iodev_add_node(iodev, node);
	cras_iodev_set_active_node(iodev, node);

	if (iodev->direction == CRAS_STREAM_OUTPUT)
		err = cras_iodev_list_add_output(iodev);
	else
		err = cras_iodev_list_add_input(iodev);
	if (err)
		goto error;

	ewma_power_disable(&iodev->ewma);

	return iodev;
error:
	if (hfpio) {
		pcm_free_base_resources(hfpio);
		free(hfpio);
	}
	return NULL;
}

void hfp_pcm_iodev_destroy(struct cras_iodev *iodev)
{
	struct fl_pcm_io *hfpio = (struct fl_pcm_io *)iodev;

	byte_buffer_destroy(&hfpio->pcm_buf);
	pcm_free_base_resources(hfpio);
	if (iodev->direction == CRAS_STREAM_OUTPUT)
		cras_iodev_list_rm_output(iodev);
	else if (iodev->direction == CRAS_STREAM_INPUT)
		cras_iodev_list_rm_input(iodev);
	cras_iodev_free_resources(iodev);
	free(hfpio);
}
