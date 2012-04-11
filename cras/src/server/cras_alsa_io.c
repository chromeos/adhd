/* Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <alsa/asoundlib.h>
#include <errno.h>
#include <limits.h>
#include <pthread.h>
#include <stdio.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <syslog.h>
#include <time.h>

#include "cras_alsa_helpers.h"
#include "cras_alsa_mixer.h"
#include "cras_config.h"
#include "cras_iodev.h"
#include "cras_iodev_list.h"
#include "cras_messages.h"
#include "cras_mix.h"
#include "cras_rclient.h"
#include "cras_rstream.h"
#include "cras_shm.h"
#include "cras_system_state.h"
#include "cras_types.h"
#include "cras_util.h"
#include "cras_volume_curve.h"
#include "utlist.h"

#define DEFAULT_BUFFER_SECONDS 2 /* default to a 2 second ALSA buffer. */
#define MIN_READ_WAIT_US 2000 /* 2ms */
#define MIN_PROCESS_TIME_US 500 /* 0.5ms - min amount of time to mix/src. */
#define SLEEP_FUZZ_FRAMES 10 /* # to consider "close enough" to sleep frames. */
#define MAX_ALSA_DEV_NAME_LENGTH 9 /* Alsa names "hw:XX,YY" + 1 for null. */

/* Holds an output for this device.  An output is a control that can be switched
 * on and off such as headphones or speakers. */
struct alsa_output_node {
	struct cras_alsa_mixer_output *mixer_output;
	struct alsa_output_node *prev, *next;
};

/* Child of cras_iodev, alsa_io handles ALSA interaction for sound devices.
 * base - The cras_iodev structure "base class".
 * dev - String that names this device (e.g. "hw:0,0").
 * device_index - ALSA index of device, Y in "hw:X:Y".
 * handle - Handle to the opened ALSA device.
 * stream_started - Has the ALSA device been started.
 * num_underruns - Number of times we have run out of data (playback only).
 * alsa_stream - Playback or capture type.
 * mixer - Alsa mixer used to control volume and mute of the device.
 * output_nodes - Alsa mixer outputs (Only used for output devices).
 * alsa_cb - Callback to fill or read samples (depends on direction).
 */
struct alsa_io {
	struct cras_iodev base;
	char *dev;
	size_t device_index;
	snd_pcm_t *handle;
	int stream_started;
	size_t num_underruns;
	snd_pcm_stream_t alsa_stream;
	struct cras_alsa_mixer *mixer;
	struct alsa_output_node *output_nodes;
	struct alsa_output_node *active_output;
	int (*alsa_cb)(struct alsa_io *aio, struct timespec *ts);
};

/* Configure the alsa device we will use. */
static int open_alsa(struct alsa_io *aio)
{
	snd_pcm_t *handle;
	int rc;
	struct cras_rstream *stream;

	/* This is called after the first stream added so configure for it. */
	stream = aio->base.streams->stream;
	if (aio->base.format == NULL)
		return -EINVAL;
	cras_rstream_get_format(stream, aio->base.format);
	aio->num_underruns = 0;

	syslog(LOG_DEBUG, "Configure alsa device %s rate %zuHz, %zu channels",
	       aio->dev, aio->base.format->frame_rate,
	       aio->base.format->num_channels);
	handle = 0; /* Avoid unused warning. */
	rc = cras_alsa_pcm_open(&handle, aio->dev, aio->alsa_stream);
	if (rc < 0)
		return rc;

	rc = cras_alsa_set_hwparams(handle, aio->base.format,
				    &aio->base.buffer_size);
	if (rc < 0) {
		cras_alsa_pcm_close(handle);
		return rc;
	}

	/* Set minimum number of available frames. */
	if (aio->base.used_size > aio->base.buffer_size)
		aio->base.used_size = aio->base.buffer_size;

	/* Configure software params. */
	rc = cras_alsa_set_swparams(handle);
	if (rc < 0) {
		cras_alsa_pcm_close(handle);
		return rc;
	}

	aio->handle = handle;

	/* Capture starts right away, playback will wait for samples. */
	if (aio->alsa_stream == SND_PCM_STREAM_CAPTURE) {
		cras_alsa_pcm_start(aio->handle);
		aio->stream_started = 1;
	}

	return 0;
}

/* Sets the volume of the playback device to the specified level. Receives a
 * volume index from the system settings, ranging from 0 to 100, converts it to
 * dB using the volume curve, and sends the dB value to alsa. Handles mute and
 * unmute, including muting when volume is zero. */
static void set_alsa_volume(void *arg)
{
	const struct alsa_io *aio = (const struct alsa_io *)arg;
	const struct cras_volume_curve *curve;
	size_t volume;
	int mute;

	assert(aio);
	if (aio->mixer == NULL)
		return;

	volume = cras_system_get_volume();
	mute = cras_system_get_mute();
	if (aio->active_output && aio->active_output->mixer_output)
		curve = aio->active_output->mixer_output->volume_curve;
	else
		curve = cras_alsa_mixer_default_volume_curve(aio->mixer);
	cras_alsa_mixer_set_dBFS(aio->mixer, curve->get_dBFS(curve, volume));
	/* Mute for zero. */
	cras_alsa_mixer_set_mute(aio->mixer, mute || (volume == 0));
}

/* Initializes the device settings and registers for callbacks when system
 * settings have been changed.
 */
static void init_device_settings(struct alsa_io *aio)
{
	/* Register for volume/mute callback and set initial volume/mute for
	 * the device. */
	cras_system_register_volume_changed_cb(set_alsa_volume, aio);
	cras_system_register_mute_changed_cb(set_alsa_volume, aio);
	set_alsa_volume(aio);
}

/*
 * Alsa I/O thread functions.
 *    These functions are all run from the audio thread.
 */

/* Handles the rm_stream message from the main thread.
 * If this is the last stream to be removed then stop the
 * audio thread and free the resources. */
static int thread_remove_stream(struct alsa_io *aio,
				struct cras_rstream *stream)
{
	int rc;

	rc = cras_iodev_delete_stream(&aio->base, stream);
	if (rc != 0)
		return rc;

	if (!cras_iodev_streams_attached(&aio->base)) {
		/* No more streams, close alsa dev. */
		cras_system_remove_volume_changed_cb(set_alsa_volume, aio);
		cras_system_remove_mute_changed_cb(set_alsa_volume, aio);
		cras_alsa_pcm_drain(aio->handle);
		cras_alsa_pcm_close(aio->handle);
		aio->handle = NULL;
		aio->stream_started = 0;
		free(aio->base.format);
		aio->base.format = NULL;
	} else {
		cras_iodev_config_params_for_streams(&aio->base);
		syslog(LOG_DEBUG,
		       "used_size %u format %u cb_threshold %u",
		       (unsigned)aio->base.used_size,
		       aio->base.format->format,
		       (unsigned)aio->base.cb_threshold);
	}

	cras_rstream_set_iodev(stream, NULL);
	return 0;
}

/* Handles the add_stream message from the main thread. */
static int thread_add_stream(struct alsa_io *aio,
			     struct cras_rstream *stream)
{
	int rc;

	/* Only allow one capture stream to attach. */
	if (aio->base.direction == CRAS_STREAM_INPUT &&
	    aio->base.streams != NULL)
		return -EBUSY;

	rc = cras_iodev_append_stream(&aio->base, stream);
	if (rc < 0)
		return rc;

	/* If not already, open alsa. */
	if (aio->handle == NULL) {
		init_device_settings(aio);

		rc = open_alsa(aio);
		if (rc < 0) {
			syslog(LOG_ERR, "Failed to open %s", aio->dev);
			cras_iodev_delete_stream(&aio->base, stream);
			return rc;
		}
	}

	cras_iodev_config_params_for_streams(&aio->base);
	return 0;
}

/* Reads any pending audio message from the socket. */
static void flush_old_aud_messages(struct cras_audio_shm_area *shm, int fd)
{
	struct audio_message msg;
	struct timespec ts = {0, 0};
	fd_set poll_set;
	int err;

	do {
		FD_ZERO(&poll_set);
		FD_SET(fd, &poll_set);
		err = pselect(fd + 1, &poll_set, NULL, NULL, &ts, NULL);
		if (err > 0 && FD_ISSET(fd, &poll_set)) {
			err = read(fd, &msg, sizeof(msg));
			shm->callback_pending = 0;
		}
	} while (err > 0);
}

/* Asks any streams with room for more data. Sets the timestamp for all streams.
 * Args:
 *    aio - The iodev containing the streams to fetch from.
 *    fetch_size - How much to fetch.
 *    alsa_delay - How much latency is queued to alsa(frames)
 * Returns:
 *    0 on success, negative error on failure. If failed, can assume that all
 *    streams have been removed from the device.
 */
static int fetch_and_set_timestamp(struct alsa_io *aio, size_t fetch_size,
				   size_t alsa_delay)
{
	size_t fr_rate, frames_in_buff;
	struct cras_io_stream *curr, *tmp;
	int rc;

	fr_rate = aio->base.format->frame_rate;

	DL_FOREACH_SAFE(aio->base.streams, curr, tmp) {
		if (curr->shm->callback_pending)
			flush_old_aud_messages(curr->shm, curr->fd);

		frames_in_buff = cras_shm_get_frames(curr->shm);

		cras_iodev_set_playback_timestamp(fr_rate,
						  frames_in_buff + alsa_delay,
						  &curr->shm->ts);

		/* If we already have enough data, don't poll this stream. */
		if (frames_in_buff >= fetch_size)
			continue;

		if (!curr->shm->callback_pending &&
		    cras_shm_is_buffer_available(curr->shm)) {
			rc = cras_rstream_request_audio(curr->stream,
							fetch_size);
			if (rc < 0) {
				thread_remove_stream(aio, curr->stream);
				/* If this failed and was the last stream,
				 * return, otherwise, on to the next one */
				if (!cras_iodev_streams_attached(&aio->base))
					return -EIO;
				continue;
			}
			curr->shm->callback_pending = 1;
		}
	}

	return 0;
}

/* Fill the buffer with samples from the attached streams.
 * Args:
 *    aio - The device to write new samples to.
 *    dst - The buffer to put the samples in (returned from snd_pcm_mmap_begin)
 *    level - The number of frames still in alsa buffer.
 *    write_limit - The maximum number of frames to write to dst.
 *    fetch_limit - The maximum number of frames to fetch from client.
 *
 * Returns:
 *    The number of frames rendered on success, a negative error code otherwise.
 *    This number of frames is the minimum of the amount of frames each stream
 *    could provide which is the maximum that can currently be rendered.
 */
static int write_streams(struct alsa_io *aio, uint8_t *dst, size_t level,
			 size_t write_limit, size_t fetch_limit)
{
	struct cras_io_stream *curr, *tmp;
	struct timeval to;
	fd_set poll_set, this_set;
	size_t streams_wait, num_mixed;
	int max_fd;
	int rc;

	/* Timeout on reading before we under-run. Leaving time to mix. */
	to.tv_sec = 0;
	to.tv_usec = (level * 1000000 / aio->base.format->frame_rate);
	if (to.tv_usec > MIN_PROCESS_TIME_US)
		to.tv_usec -= MIN_PROCESS_TIME_US;
	if (to.tv_usec < MIN_READ_WAIT_US)
		to.tv_usec = MIN_READ_WAIT_US;

	FD_ZERO(&poll_set);
	max_fd = -1;
	streams_wait = 0;
	num_mixed = 0;

	/* Check if streams have enough data to fill this request,
	 * if not, wait for them. Mix all streams we have enough data for. */
	DL_FOREACH(aio->base.streams, curr) {
		curr->mixed = 0;
		if (cras_shm_get_frames(curr->shm) < write_limit &&
		    curr->shm->callback_pending) {
			/* Not enough to mix this call, wait for a response. */
			streams_wait++;
			FD_SET(curr->fd, &poll_set);
			if (curr->fd > max_fd)
				max_fd = curr->fd;
		} else {
			curr->mixed = cras_mix_add_stream(
				curr->shm,
				aio->base.format->num_channels,
				dst, &write_limit, &num_mixed);
		}
	}

	/* Wait until all polled clients reply, or a timeout. */
	while (streams_wait > 0) {
		this_set = poll_set;
		rc = select(max_fd + 1, &this_set, NULL, NULL, &to);
		if (rc <= 0) {
			/* Timeout */
			DL_FOREACH(aio->base.streams, curr) {
				if (curr->shm->callback_pending &&
				    FD_ISSET(curr->fd, &poll_set))
					curr->shm->num_cb_timeouts++;
			}
			break;
		}
		DL_FOREACH_SAFE(aio->base.streams, curr, tmp) {
			if (!FD_ISSET(curr->fd, &this_set))
				continue;

			FD_CLR(curr->fd, &poll_set);
			streams_wait--;
			curr->shm->callback_pending = 0;
			rc = cras_rstream_get_audio_request_reply(curr->stream);
			if (rc < 0) {
				thread_remove_stream(aio, curr->stream);
				if (!cras_iodev_streams_attached(&aio->base))
					return -EIO;
				continue;
			}
			if (curr->mixed)
				continue;
			curr->mixed = cras_mix_add_stream(
				curr->shm,
				aio->base.format->num_channels,
				dst, &write_limit, &num_mixed);
		}
	}

	if (num_mixed == 0)
		return num_mixed;

	/* For all streams rendered, mark the data used as read. */
	DL_FOREACH(aio->base.streams, curr)
		if (curr->mixed)
			cras_shm_buffer_read(curr->shm, write_limit);

	return write_limit;
}

/* Ask any clients that have room for more data in the buffer to send some. */
void get_data_from_other_streams(struct alsa_io *aio, size_t alsa_used)
{
	struct cras_io_stream *curr, *tmp;
	size_t frames;
	int rc;

	DL_FOREACH_SAFE(aio->base.streams, curr, tmp) {
		if (curr->shm->callback_pending)
			continue;

		frames = cras_shm_get_frames(curr->shm) + alsa_used;

		if (frames <= cras_rstream_get_cb_threshold(curr->stream) &&
		    cras_shm_is_buffer_available(curr->shm)) {
			rc = cras_rstream_request_audio_buffer(curr->stream);
			if (rc < 0) {
				thread_remove_stream(aio, curr->stream);
				/* If this failed and was the last stream,
				 * return, otherwise, on to the next one */
				if (!cras_iodev_streams_attached(&aio->base))
					return;
			} else
				curr->shm->callback_pending = 1;
		}
	}
}

/* Check if we should get more samples for playback from the source streams. If
 * more data is needed by the output, fetch and render it.
 * Args:
 *    ts - how long to sleep before calling this again.
 * Returns:
 *    0 if successful, otherwise a negative error.  If an error occurs you can
 *    assume that the pcm is no longer functional.
 */
static int possibly_fill_audio(struct alsa_io *aio,
			       struct timespec *ts)
{
	snd_pcm_uframes_t frames, used, fr_to_req;
	snd_pcm_sframes_t written, delay;
	snd_pcm_uframes_t total_written = 0;
	snd_pcm_uframes_t offset = 0;
	snd_pcm_t *handle = aio->handle;
	int rc;
	size_t fr_bytes;
	uint8_t *dst = NULL;

	ts->tv_sec = ts->tv_nsec = 0;
	fr_bytes = cras_get_format_bytes(aio->base.format);

	frames = 0;
	rc = cras_alsa_get_avail_frames(aio->handle,
					aio->base.buffer_size,
					&frames);
	if (rc < 0)
		return rc;
	used = aio->base.buffer_size - frames;

	/* Make sure we should actually be awake right now (or close enough) */
	if (used > aio->base.cb_threshold + SLEEP_FUZZ_FRAMES) {
		/* Check if the pcm is still running. */
		if (snd_pcm_state(aio->handle) == SND_PCM_STATE_SUSPENDED) {
			aio->stream_started = 0;
			rc = cras_alsa_attempt_resume(aio->handle);
			if (rc < 0)
				return rc;
		}
		cras_iodev_fill_time_from_frames(used,
						 aio->base.cb_threshold,
						 aio->base.format->frame_rate,
						 ts);
		return 0;
	}

	/* check the current delay through alsa */
	rc = cras_alsa_get_delay_frames(aio->handle,
					aio->base.buffer_size,
					&delay);
	if (rc < 0)
		return rc;

	/* Request data from streams that need more */
	fr_to_req = aio->base.used_size - used;
	rc = fetch_and_set_timestamp(aio, fr_to_req, delay);
	if (rc < 0)
		return rc;

	/* Have to loop writing to alsa, will be at most 2 loops, this only
	 * happens when the circular buffer is at the end and returns us a
	 * partial area to write to from mmap_begin */
	while (total_written < fr_to_req) {
		frames = fr_to_req - total_written;
		rc = cras_alsa_mmap_begin(aio->handle, fr_bytes, &dst, &offset,
					  &frames, &aio->num_underruns);
		if (rc < 0)
			return rc;

		written = write_streams(aio, dst, used + total_written,
					frames, fr_to_req);
		if (written < 0) /* pcm has been closed */
			return (int)written;

		if (written < (snd_pcm_sframes_t)frames)
			/* Got all the samples from client that we can, but it
			 * won't fill the request. */
			fr_to_req = 0; /* break out after committing samples */

		rc = cras_alsa_mmap_commit(aio->handle, offset, written,
					   &aio->num_underruns);
		if (rc < 0)
			return rc;
		total_written += written;
	}

	/* If we haven't started alsa and we wrote samples, then start it up. */
	if (!aio->stream_started && total_written > 0) {
		rc = cras_alsa_pcm_start(handle);
		if (rc < 0) {
			syslog(LOG_ERR, "Start error: %s", snd_strerror(rc));
			return rc;
		}
		aio->stream_started = 1;
	}

	/* Ask any clients that have room to fill up. */
	get_data_from_other_streams(aio, total_written + used);

	/* Set the sleep time based on how much is left to play */
	cras_iodev_fill_time_from_frames(total_written + used,
					 aio->base.cb_threshold,
					 aio->base.format->frame_rate,
					 ts);

	return 0;
}

/* Pass captured samples to the client.
 * Args:
 *    src - the memory area containing the captured samples.
 *    offset - offset into buffer (from mmap_begin).
 *    count - the number of frames captured = buffer_frames.
 *
 * Returns:
 *    The number of frames passed to the client on success, a negative error
 *      code otherwise.
 */
static snd_pcm_sframes_t read_streams(struct alsa_io *aio,
				      const uint8_t *src,
				      snd_pcm_uframes_t offset,
				      snd_pcm_uframes_t count)
{
	struct cras_io_stream *streams, *curr;
	struct cras_audio_shm_area *shm;
	snd_pcm_sframes_t delay;
	uint8_t *dst;
	int rc;

	streams = aio->base.streams;
	if (!streams)
		return 0; /* Nowhere to put samples. */

	curr = streams;
	shm = curr->shm;

	rc = cras_alsa_get_delay_frames(aio->handle,
					aio->base.buffer_size,
					&delay);
	if (rc < 0)
		return rc;

	cras_iodev_set_capture_timestamp(aio->base.format->frame_rate,
					 delay,
					 &shm->ts);

	dst = cras_shm_get_curr_write_buffer(shm);
	memcpy(dst, src, count * shm->frame_bytes);
	cras_shm_buffer_written(shm, count);
	if (shm->write_offset == shm->read_offset)
		shm->num_overruns++;

	rc = cras_rstream_audio_ready(curr->stream, count);
	if (rc < 0) {
		thread_remove_stream(aio, curr->stream);
		return rc;
	}
	return count;
}

/* The capture path equivalent of possibly_fill_audio
 * Read audio cb_threshold samples at a time, then sleep, until next
 * cb_samples have been received.
 * Args:
 *    ts - fills with how long to sleep before calling this again.
 * Returns:
 *    0 unless there is an error talking to alsa or the client.
 */
static int possibly_read_audio(struct alsa_io *aio,
			       struct timespec *ts)
{
	snd_pcm_uframes_t frames, nread, num_to_read, remainder;
	snd_pcm_uframes_t offset = 0;
	int rc;
	uint64_t to_sleep;
	size_t fr_bytes;
	uint8_t *src;

	ts->tv_sec = ts->tv_nsec = 0;
	fr_bytes = cras_get_format_bytes(aio->base.format);

	rc = cras_alsa_get_avail_frames(aio->handle,
					aio->base.buffer_size,
					&frames);
	if (rc < 0)
		return rc;

	num_to_read = aio->base.cb_threshold;
	if (frames < aio->base.cb_threshold)
		num_to_read = 0;

	while (num_to_read > 0) {
		nread = num_to_read;
		rc = cras_alsa_mmap_begin(aio->handle, fr_bytes, &src, &offset,
					  &nread, &aio->num_underruns);
		if (rc < 0 || nread == 0)
			return rc;

		rc = read_streams(aio, src, offset, nread);
		if (rc < 0)
			return rc;

		rc = cras_alsa_mmap_commit(aio->handle, offset, nread,
					   &aio->num_underruns);
		if (rc < 0)
			return rc;
		num_to_read -= nread;
	}

	/* Adjust sleep time to target our callback threshold. */
	remainder = frames - aio->base.cb_threshold;
	if (frames < aio->base.cb_threshold)
		remainder = 0;

	to_sleep = (aio->base.cb_threshold - remainder);
	if (aio->base.cb_threshold < remainder)
		to_sleep = 0;
	ts->tv_nsec = to_sleep * 1000000 / aio->base.format->frame_rate;
	ts->tv_nsec *= 1000;
	return 0;
}

/* Stop the playback thread */
static int terminate_pb_thread(struct alsa_io *aio)
{
	pthread_exit(0);
}

/* Handle a message sent to the playback thread */
static int handle_playback_thread_message(struct alsa_io *aio)
{
	uint8_t buf[256];
	struct cras_iodev_msg *msg = (struct cras_iodev_msg *)buf;
	int ret = 0;
	int err;

	err = cras_iodev_read_thread_command(&aio->base, buf, 256);
	if (err < 0)
		return err;

	switch (msg->id) {
	case CRAS_IODEV_ADD_STREAM: {
		struct cras_iodev_add_rm_stream_msg *amsg;
		amsg = (struct cras_iodev_add_rm_stream_msg *)msg;
		ret = thread_add_stream(aio, amsg->stream);
		break;
	}
	case CRAS_IODEV_RM_STREAM: {
		struct cras_iodev_add_rm_stream_msg *rmsg;
		const struct cras_audio_shm_area *shm;

		rmsg = (struct cras_iodev_add_rm_stream_msg *)msg;
		shm = cras_rstream_get_shm(rmsg->stream);
		if (shm != NULL) {
			syslog(LOG_DEBUG, "cb_timeouts:%zu",
			       shm->num_cb_timeouts);
			syslog(LOG_DEBUG, "overruns:%zu", shm->num_overruns);
		}
		ret = thread_remove_stream(aio, rmsg->stream);
		if (ret < 0)
			syslog(LOG_ERR, "Failed to remove the stream");
		syslog(LOG_DEBUG, "underruns:%zu", aio->num_underruns);
		break;
	}
	case CRAS_IODEV_STOP:
		ret = 0;
		err = cras_iodev_send_command_response(&aio->base, ret);
		if (err < 0)
			return err;
		terminate_pb_thread(aio);
		break;
	default:
		ret = -EINVAL;
		break;
	}

	err = cras_iodev_send_command_response(&aio->base, ret);
	if (err < 0)
		return err;
	return ret;
}

/* For playback, fill the audio buffer when needed, for capture, pull out
 * samples when they are ready.
 * This thread will attempt to run at a high priority to allow for low latency
 * streams.  This thread sleeps while alsa plays back or captures audio, it
 * will wake up as little as it can while avoiding xruns.  It can also be woken
 * by sending it a message using the
 * "cras_iodev_post_message_to_playback_thread" function.
 */
static void *alsa_io_thread(void *arg)
{
	struct alsa_io *aio = (struct alsa_io *)arg;
	struct timespec ts;
	fd_set poll_set;
	int msg_fd;
	int err;

	msg_fd = cras_iodev_get_thread_poll_fd(&aio->base);

	/* Attempt to get realtime scheduling */
	if (cras_set_rt_scheduling(CRAS_SERVER_RT_THREAD_PRIORITY) == 0)
		cras_set_thread_priority(CRAS_SERVER_RT_THREAD_PRIORITY);

	while (1) {
		struct timespec *wait_ts;

		wait_ts = NULL;

		if (aio->handle) {
			/* alsa opened */
			err = aio->alsa_cb(aio, &ts);
			if (err < 0)
				syslog(LOG_ERR, "alsa cb error %d", err);
			wait_ts = &ts;
		}

		FD_ZERO(&poll_set);
		FD_SET(msg_fd, &poll_set);
		err = pselect(msg_fd + 1, &poll_set, NULL, NULL, wait_ts, NULL);
		if (err > 0 && FD_ISSET(msg_fd, &poll_set)) {
			err = handle_playback_thread_message(aio);
			if (err < 0)
				syslog(LOG_ERR, "handle message %d", err);
		}
	}

	return NULL;
}

/*
 * Functions run in the main server context.
 */

/* Frees resources used by the alsa iodev.
 * Args:
 *    iodev - the iodev to free the resources from.
 */
static void free_alsa_iodev_resources(struct alsa_io *aio)
{
	struct alsa_output_node *output, *tmp;

	cras_iodev_deinit(&aio->base);
	free(aio->base.supported_rates);
	free(aio->base.supported_channel_counts);
	DL_FOREACH_SAFE(aio->output_nodes, output, tmp) {
		DL_DELETE(aio->output_nodes, output);
		free(output);
	}
	free(aio->dev);
}

/* Callback for listing mixer outputs.  The mixer will call this once for each
 * output associated with this device.  Most commonly this is used to tell the
 * device it has Headphones and Speakers. */
static void new_output(struct cras_alsa_mixer_output *cras_output,
		       void *callback_arg)
{
	struct alsa_io *aio;
	struct alsa_output_node *output;

	aio = (struct alsa_io *)callback_arg;
	if (aio == NULL) {
		syslog(LOG_ERR, "Invalid aio when listing outputs.");
		return;
	}
	output = (struct alsa_output_node *)calloc(1, sizeof(*output));
	if (output == NULL) {
		syslog(LOG_ERR, "Out of memory when listing outputs.");
		return;
	}

	output->mixer_output = cras_output;
	DL_APPEND(aio->output_nodes, output);
}

/*
 * Exported Interface.
 */

struct cras_iodev *alsa_iodev_create(size_t card_index,
				     size_t device_index,
				     struct cras_alsa_mixer *mixer,
				     int auto_route,
				     enum CRAS_STREAM_DIRECTION direction)
{
	struct alsa_io *aio;
	struct cras_iodev *iodev;
	int err;

	aio = (struct alsa_io *)calloc(1, sizeof(*aio));
	if (!aio)
		return NULL;
	iodev = &aio->base;
	aio->device_index = device_index;
	aio->dev = (char *)malloc(MAX_ALSA_DEV_NAME_LENGTH);
	if (aio->dev == NULL)
		goto cleanup_iodev;
	snprintf(aio->dev,
		 MAX_ALSA_DEV_NAME_LENGTH,
		 "hw:%zu,%zu",
		 card_index,
		 device_index);

	if (cras_iodev_init(iodev, direction, alsa_io_thread, aio))
		goto cleanup_iodev;

	if (direction == CRAS_STREAM_INPUT) {
		aio->alsa_stream = SND_PCM_STREAM_CAPTURE;
		aio->alsa_cb = possibly_read_audio;
	} else {
		assert(direction == CRAS_STREAM_OUTPUT);
		aio->alsa_stream = SND_PCM_STREAM_PLAYBACK;
		aio->alsa_cb = possibly_fill_audio;
	}

	err = cras_alsa_fill_properties(aio->dev, aio->alsa_stream,
					&iodev->supported_rates,
					&iodev->supported_channel_counts);
	if (err < 0 || iodev->supported_rates[0] == 0 ||
	    iodev->supported_channel_counts[0] == 0) {
		syslog(LOG_ERR, "cras_alsa_fill_properties: %s", strerror(err));
		goto cleanup_iodev;
	}

	aio->mixer = mixer;
	cras_alsa_mixer_list_outputs(mixer, device_index, new_output, aio);

	/* If we don't have separate outputs just make a default one. */
	if (aio->output_nodes == NULL) {
		new_output(NULL, aio);
		aio->active_output = aio->output_nodes;
	}

	/* Finally add it to the approriate iodev list. */
	if (direction == CRAS_STREAM_INPUT)
		cras_iodev_list_add_input(&aio->base, auto_route);
	else {
		assert(direction == CRAS_STREAM_OUTPUT);
		cras_iodev_list_add_output(&aio->base, auto_route);
	}

	return &aio->base;

cleanup_iodev:
	free_alsa_iodev_resources(aio);
	free(aio);
	return NULL;
}

void alsa_iodev_destroy(struct cras_iodev *iodev)
{
	struct alsa_io *aio = (struct alsa_io *)iodev;
	int rc;

	if (iodev->direction == CRAS_STREAM_INPUT)
		rc = cras_iodev_list_rm_input(iodev);
	else {
		assert(iodev->direction == CRAS_STREAM_OUTPUT);
		rc = cras_iodev_list_rm_output(iodev);
	}
	free_alsa_iodev_resources(aio);
	if (rc == 0)
		free(iodev);
}

int alsa_iodev_set_active_output(struct cras_iodev *iodev,
				 struct cras_alsa_mixer_output *active)
{
	struct alsa_io *aio = (struct alsa_io *)iodev;
	struct alsa_output_node *output;
	int found_output = 0;

	cras_alsa_mixer_set_mute(aio->mixer, 1);
	/* Unmute the acrtive input, mute all others. */
	DL_FOREACH(aio->output_nodes, output) {
		if (output->mixer_output == NULL)
			continue;
		if (output->mixer_output == active) {
			aio->active_output = output;
			found_output = 1;
		}
		cras_alsa_mixer_set_output_active_state(
				output->mixer_output,
				output->mixer_output == active);
	}
	if (!found_output) {
		syslog(LOG_ERR, "Attempt to switch to non-existant output.");
		return -EINVAL;
	}
	/* Setting the volume will also unmute if the system isn't muted. */
	set_alsa_volume(aio);
	return 0;
}
