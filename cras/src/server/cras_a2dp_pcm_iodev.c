/* Copyright 2021 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

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
#include "cras_iodev.h"
#include "cras_util.h"
#include "sfh.h"
#include "utlist.h"

#define PCM_BUF_MAX_SIZE_FRAMES (4096 * 4)
#define PCM_BUF_MAX_SIZE_BYTES (PCM_BUF_MAX_SIZE_FRAMES * 4)

/* Floss currently set a 10ms poll interval as A2DP_DATA_READ_POLL_MS.
 * Double it and use for scheduling here. */
#define PCM_BLOCK_MS 20

/* Threshold for reasonable a2dp throttle log in audio dump. */
static const struct timespec throttle_log_threshold = {
	0, 10000000 /* 10ms */
};

/* Threshold for severe a2dp throttle event. */
static const struct timespec throttle_event_threshold = {
	2, 0 /* 2s */
};

/* Child of cras_iodev to handle bluetooth A2DP streaming.
 * Members:
 *    base - The cras_iodev structure "base class"
 *    audio_fd - The sockets for device to read and write
 *    sock_depth_frames - Socket depth of the a2dp pcm socket.
 *    ncm_buf - Buffer to hold pcm samples before encode.
 *    next_flush_time - The time when it is okay for next flush call.
 *    flush_period - The time period between two a2dp packet writes.
 *    write_block - How many frames of audio samples we prefer to write in one
 *        socket write.
 *    a2dp - The associated cras_a2dp object.
 */
struct a2dp_io {
	struct cras_iodev base;
	int audio_fd;
	unsigned sock_depth_frames;
	struct byte_buffer *pcm_buf;
	struct timespec next_flush_time;
	struct timespec flush_period;
	unsigned int write_block;
	struct cras_a2dp *a2dp;
};

static int flush(const struct cras_iodev *iodev);

static int update_supported_formats(struct cras_iodev *iodev)
{
	/* Supported formats are fixed when iodev created. */
	return 0;
}

static unsigned int bt_local_queued_frames(const struct cras_iodev *iodev)
{
	struct a2dp_io *a2dpio = (struct a2dp_io *)iodev;
	return buf_queued(a2dpio->pcm_buf) /
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
static int enter_no_stream(struct a2dp_io *a2dpio)
{
	struct cras_iodev *odev = &a2dpio->base;
	int rc;
	/*
         * Setting target level to 3 times of min_buffer_level.
         * We want hw_level to stay bewteen 1-2 times of min_buffer_level on
	 * top of the underrun threshold(i.e one min_cb_level).
         */
	rc = fill_zeros_to_target_level(odev, 3 * odev->min_buffer_level);
	if (rc)
		syslog(LOG_ERR, "Error in A2DP enter_no_stream");
	return flush(odev);
}

/*
 * This is called when stream data is available to write. Prepare audio
 * data to one min_buffer_level. Don't flush it now because stream data is
 * coming right up which will trigger next flush at appropriate time.
 */
static int leave_no_stream(struct a2dp_io *a2dpio)
{
	struct cras_iodev *odev = &a2dpio->base;

	/*
	 * Since stream data is ready, just make sure hw_level doesn't underrun
	 * after one flush. Hence setting the target level to 2 times of
	 * min_buffer_level.
         */
	return fill_zeros_to_target_level(odev, 2 * odev->min_buffer_level);
}

/*
 * Makes sure there's enough data(zero frames) to flush when no stream presents.
 * Note that the underrun condition is when real buffer level goes below
 * min_buffer_level, so we want to keep data at a reasonable higher level on top
 * of that.
 */
static int no_stream(struct cras_iodev *odev, int enable)
{
	struct a2dp_io *a2dpio = (struct a2dp_io *)odev;

	if (enable)
		return enter_no_stream(a2dpio);
	else
		return leave_no_stream(a2dpio);
}

/*
 * To be called when a2dp socket becomes writable.
 */
static int a2dp_socket_write_cb(void *arg, int revent)
{
	struct cras_iodev *iodev = (struct cras_iodev *)arg;
	return flush(iodev);
}

static int configure_dev(struct cras_iodev *iodev)
{
	struct a2dp_io *a2dpio = (struct a2dp_io *)iodev;
	int sock_depth;
	int rc;
	socklen_t optlen;
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

	a2dpio->pcm_buf = byte_buffer_create(PCM_BUF_MAX_SIZE_BYTES);
	if (!a2dpio->pcm_buf)
		return -ENOMEM;

	getsockopt(a2dpio->audio_fd, SOL_SOCKET, SO_SNDBUF, &sock_depth,
		   &optlen);
	a2dpio->sock_depth_frames = sock_depth / format_bytes;

	/* Configure write_block to frames equivalent to PCM_BLOCK_MS. */
	a2dpio->write_block = iodev->format->frame_rate * PCM_BLOCK_MS / 1000;

	/* Initialize flush_period by write_block, it will be changed
	 * later based on socket write schedule. */
	cras_frames_to_time(a2dpio->write_block, iodev->format->frame_rate,
			    &a2dpio->flush_period);

	iodev->buffer_size = PCM_BUF_MAX_SIZE_FRAMES;

	/*
	 * As we directly write pcm here, there is no min buffer limitation.
	 */
	iodev->min_buffer_level = 0;

	audio_thread_add_events_callback(a2dpio->audio_fd, a2dp_socket_write_cb,
					 iodev, POLLOUT | POLLERR | POLLHUP);
	audio_thread_config_events_callback(a2dpio->audio_fd, TRIGGER_NONE);
	return 0;
}

static int start(const struct cras_iodev *iodev)
{
	struct a2dp_io *a2dpio = (struct a2dp_io *)iodev;

	/*
	 * This is called when iodev in open state, at the moment when
	 * output sample is ready. Initialize the next_flush_time for
	 * following flush calls.
	 */
	clock_gettime(CLOCK_MONOTONIC_RAW, &a2dpio->next_flush_time);

	return 0;
}

static int close_dev(struct cras_iodev *iodev)
{
	int err;
	struct a2dp_io *a2dpio = (struct a2dp_io *)iodev;

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

static unsigned int frames_to_play_in_sleep(struct cras_iodev *iodev,
					    unsigned int *hw_level,
					    struct timespec *hw_tstamp)
{
	struct a2dp_io *a2dpio = (struct a2dp_io *)iodev;
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
	struct a2dp_io *a2dpio;
	struct timespec now, ts;
	static const struct timespec flush_wake_fuzz_ts = {
		0, 1000000 /* 1ms */
	};

	a2dpio = (struct a2dp_io *)iodev;

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
	const struct a2dp_io *a2dpio = (struct a2dp_io *)iodev;
	struct timespec tstamp;

	/* The number of frames in the pcm buffer plus socket depth */
	return frames_queued(iodev, &tstamp) + a2dpio->sock_depth_frames;
}

static int get_buffer(struct cras_iodev *iodev, struct cras_audio_area **area,
		      unsigned *frames)
{
	size_t format_bytes;
	struct a2dp_io *a2dpio;

	a2dpio = (struct a2dp_io *)iodev;

	format_bytes = cras_get_format_bytes(iodev->format);

	if (iodev->direction != CRAS_STREAM_OUTPUT)
		return 0;

	*frames = MIN(*frames, buf_writable(a2dpio->pcm_buf) / format_bytes);
	iodev->area->frames = *frames;
	cras_audio_area_config_buf_pointers(iodev->area, iodev->format,
					    buf_write_pointer(a2dpio->pcm_buf));
	*area = iodev->area;
	return 0;
}

static int put_buffer(struct cras_iodev *iodev, unsigned nwritten)
{
	size_t written_bytes;
	size_t format_bytes;
	struct a2dp_io *a2dpio = (struct a2dp_io *)iodev;

	format_bytes = cras_get_format_bytes(iodev->format);
	written_bytes = nwritten * format_bytes;

	if (written_bytes > buf_writable(a2dpio->pcm_buf))
		return -EINVAL;

	buf_increment_write(a2dpio->pcm_buf, written_bytes);

	return flush(iodev);
}

static int flush_buffer(struct cras_iodev *iodev)
{
	return 0;
}

static void set_volume(struct cras_iodev *iodev)
{
}

static void update_active_node(struct cras_iodev *iodev, unsigned node_idx,
			       unsigned dev_enabled)
{
}

void a2dp_pcm_free_resources(struct a2dp_io *a2dpio)
{
	struct cras_ionode *node;

	node = a2dpio->base.active_node;
	if (node) {
		cras_iodev_rm_node(&a2dpio->base, node);
		free(node);
	}
	free(a2dpio->base.supported_channel_counts);
	free(a2dpio->base.supported_rates);
	free(a2dpio->base.supported_formats);
}

struct cras_iodev *a2dp_pcm_iodev_create(struct cras_a2dp *a2dp,
					 int sample_rate, int bits_per_sample,
					 int channel_mode)
{
	int err;
	struct a2dp_io *a2dpio;
	struct cras_iodev *iodev;
	struct cras_ionode *node;
	const char *addr, *name;

	a2dpio = (struct a2dp_io *)calloc(1, sizeof(*a2dpio));
	if (!a2dpio)
		goto error;

	iodev = &a2dpio->base;

	a2dpio->audio_fd = -1;
	a2dpio->a2dp = a2dp;

	/* A2DP only does output now */
	iodev->direction = CRAS_STREAM_OUTPUT;

	name = cras_floss_a2dp_get_display_name(a2dp);
	snprintf(iodev->info.name, sizeof(iodev->info.name), "%s", name);
	iodev->info.name[ARRAY_SIZE(iodev->info.name) - 1] = '\0';

	/* Address determines the unique stable id. */
	addr = cras_floss_a2dp_get_addr(a2dp);
	iodev->info.stable_id = SuperFastHash(addr, strlen(addr), strlen(addr));

	iodev->configure_dev = configure_dev;
	iodev->frames_queued = frames_queued;
	iodev->delay_frames = delay_frames;
	iodev->get_buffer = get_buffer;
	iodev->put_buffer = put_buffer;
	iodev->flush_buffer = flush_buffer;
	iodev->no_stream = no_stream;
	iodev->output_underrun = output_underrun;
	iodev->close_dev = close_dev;
	iodev->update_supported_formats = update_supported_formats;
	iodev->update_active_node = update_active_node;
	iodev->set_volume = set_volume;
	iodev->start = start;
	iodev->frames_to_play_in_sleep = frames_to_play_in_sleep;

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
		a2dp_pcm_free_resources(a2dpio);
		free(a2dpio);
	}
	return NULL;
}

void a2dp_pcm_iodev_destroy(struct cras_iodev *iodev)
{
	struct a2dp_io *a2dpio = (struct a2dp_io *)iodev;

	/* Free resources when device successfully removed. */
	a2dp_pcm_free_resources(a2dpio);
	cras_iodev_list_rm_output(iodev);
	cras_iodev_free_resources(iodev);
	free(a2dpio);
}
