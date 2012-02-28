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
#include "cras_system_settings.h"
#include "cras_types.h"
#include "cras_util.h"
#include "cras_volume_curve.h"
#include "utlist.h"

#define DEFAULT_BUFFER_SECONDS 2 /* default to a 2 second ALSA buffer. */
#define MIN_READ_WAIT_US 2000 /* 2ms */
#define MIN_PROCESS_TIME_US 500 /* 0.5ms - min amount of time to mix/src. */
#define SLEEP_FUZZ_FRAMES 10 /* # to consider "close enough" to sleep frames. */

/* Child of cras_iodev, alsa_io handles ALSA interaction for sound devices.
 * base - The cras_iodev structure "base class".
 * dev - String that names this device (e.g. "hw:0,0").
 * handle - Handle to the opened ALSA device.
 * buffer_size - Size of the ALSA buffer in frames.
 * used_size - Number of frames that are used for audio.
 * cb_threshold - Level below which to call back to the client (in frames).
 * min_cb_level - Minimum amount of free frames to tell the client about.
 * tid - Thread ID of the running ALSA thread.
 * stream_started - Has the ALSA device been started.
 * num_underruns - Number of times we have run out of data (playback only).
 * to_thread_fds - Send a message from main to running thread.
 * to_main_fds - Send a message to main from running thread.
 * alsa_stream - Playback or capture type.
 * mixer - Alsa mixer used to control volume and mute of the device.
 * alsa_cb - Callback to fill or read samples (depends on direction).
 */
struct alsa_io {
	struct cras_iodev base;
	char *dev;
	snd_pcm_t *handle;
	snd_pcm_uframes_t buffer_size;
	snd_pcm_uframes_t used_size;
	snd_pcm_uframes_t cb_threshold;
	snd_pcm_sframes_t min_cb_level;
	pthread_t tid;
	int stream_started;
	size_t num_underruns;
	int to_thread_fds[2];
	int to_main_fds[2];
	snd_pcm_stream_t alsa_stream;
	struct cras_alsa_mixer *mixer;
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
				    &aio->buffer_size);
	if (rc < 0) {
		cras_alsa_pcm_close(handle);
		return rc;
	}

	/* Set minimum number of available frames. */
	if (aio->used_size > aio->buffer_size)
		aio->used_size = aio->buffer_size;

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
 * dB using the volume curve, and sends the dB value to alsa. */
static void set_alsa_volume(size_t volume, void *arg)
{
	const struct alsa_io *aio = (const struct alsa_io *)arg;
	assert(aio);
	if (aio->mixer == NULL)
		return;
	cras_alsa_mixer_set_dBFS(aio->mixer,
		cras_volume_curve_get_dBFS_for_index(volume));
	/* Mute for zero. */
	cras_alsa_mixer_set_mute(aio->mixer,
				 cras_system_get_mute() || volume == 0);
}

/* Sets the mute of the playback device to the specified level. */
static void set_alsa_mute(int mute, void *arg)
{
	const struct alsa_io *aio = (const struct alsa_io *)arg;
	assert(aio);
	if (aio->mixer == NULL)
		return;
	cras_alsa_mixer_set_mute(aio->mixer, mute);
}

/* Initializes the device settings and registers for callbacks when system
 * settings have been changed.
 */
static void init_device_settings(struct alsa_io *aio)
{
	/* Register for volume/mute callback and set initial volume/mute for
	 * the device. */
	cras_system_register_volume_changed_cb(set_alsa_volume, aio);
	cras_system_register_mute_changed_cb(set_alsa_mute, aio);
	set_alsa_mute(cras_system_get_mute(), aio);
	set_alsa_volume(cras_system_get_volume(), aio);
}

/* Configures when to wake up, the minimum amount free before refilling, and
 * other params that are independent of alsa configuration. */
static void config_alsa_iodev_params(struct alsa_io *aio)
{
	const struct cras_io_stream *lowest, *curr;
	/* Base settings on the lowest-latency stream. */
	lowest = aio->base.streams;
	DL_FOREACH(aio->base.streams, curr)
		if (cras_rstream_get_buffer_size(curr->stream) <
		    cras_rstream_get_buffer_size(lowest->stream))
			lowest = curr;

	aio->used_size = cras_rstream_get_buffer_size(lowest->stream);
	aio->cb_threshold = cras_rstream_get_cb_threshold(lowest->stream);
	aio->min_cb_level = cras_rstream_get_min_cb_level(lowest->stream);

	syslog(LOG_DEBUG,
	       "used_size %u format %u cb_threshold %u min_cb_level %u",
	       (unsigned)aio->used_size, aio->base.format->format,
	       (unsigned)aio->cb_threshold, (unsigned)aio->min_cb_level);
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
		cras_alsa_pcm_drain(aio->handle);
		cras_alsa_pcm_close(aio->handle);
		aio->handle = NULL;
		aio->stream_started = 0;
		free(aio->base.format);
		aio->base.format = NULL;
	} else
		config_alsa_iodev_params(aio);
	return 0;
}

/* Handles the add_stream message from the main thread. */
static int thread_add_stream(struct alsa_io *aio,
			     struct cras_rstream *stream)
{
	int rc;

	/* Only allow one capture stream to attach. */
	if (aio->base.direction == CRAS_STREAM_INPUT &&
	    aio->handle != NULL)
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

	config_alsa_iodev_params(aio);
	return 0;
}

/* Sets the timestamp for when the next sample will be rendered.  Figure
 * this out by combining the current time with the alsa latency */
static void set_playback_timestamp(size_t frame_rate,
				   size_t frames,
				   struct timespec *ts)
{
	clock_gettime(CLOCK_MONOTONIC, ts);

	/* For playback, want now + samples left to be played.
	 * ts = time next written sample will be played to DAC,
	 */
	ts->tv_nsec += frames * (1000000000L / frame_rate);
	while (ts->tv_nsec > 1000000000) {
		ts->tv_sec++;
		ts->tv_nsec -= 1000000000L;
	}
}

/* Sets the time that the first sample in the buffer was captured at the ADC. */
static void set_capture_timestamp(size_t frame_rate,
				  size_t frames,
				  struct timespec *ts)
{
	long tmp;

	clock_gettime(CLOCK_MONOTONIC, ts);

	/* For capture, now - samples left to be read.
	 * ts = time next sample to be read was captured at ADC.
	 */
	tmp = frames * (1000000000L / frame_rate);
	while (tmp > 1000000000L) {
		tmp -= 1000000000L;
		ts->tv_sec--;
	}
	if (ts->tv_nsec >= tmp)
		ts->tv_nsec -= tmp;
	else {
		tmp -= ts->tv_nsec;
		ts->tv_nsec = 1000000000L - tmp;
		ts->tv_sec--;
	}
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

		set_playback_timestamp(fr_rate, frames_in_buff + alsa_delay,
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

/* Fill timespec ts with the time to sleep based on the number of frames and
 * frame rate.  Threshold is how many frames should be left when the timer
 * expires */
static void fill_time_from_frames(size_t frames, size_t cb_threshold,
				  size_t frame_rate, struct timespec *ts)
{
	size_t to_play_usec;

        ts->tv_sec = 0;
	/* adjust sleep time to target our callback threshold */
	if (frames > cb_threshold)
		to_play_usec = (frames - cb_threshold) *
			1000000 / frame_rate;
	else
		to_play_usec = 0;
	ts->tv_nsec = to_play_usec * 1000;

	while (ts->tv_nsec > 1000000000L) {
		ts->tv_sec++;
		ts->tv_nsec -= 1000000000L;
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
	rc = cras_alsa_get_avail_frames(aio->handle, aio->buffer_size, &frames);
	if (rc < 0)
		return rc;
	used = aio->buffer_size - frames;

	/* Make sure we should actually be awake right now (or close enough) */
	if (used > aio->cb_threshold + SLEEP_FUZZ_FRAMES) {
		fill_time_from_frames(used, aio->cb_threshold,
			      aio->base.format->frame_rate, ts);
		return 0;
	}

	/* check the current delay through alsa */
	rc = cras_alsa_get_delay_frames(aio->handle, aio->buffer_size, &delay);
	if (rc < 0)
		return rc;

	/* Request data from streams that need more */
	fr_to_req = aio->used_size - used;
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
	fill_time_from_frames(total_written + used, aio->cb_threshold,
			      aio->base.format->frame_rate, ts);

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

	rc = cras_alsa_get_delay_frames(aio->handle, aio->buffer_size, &delay);
	if (rc < 0)
		return rc;

	set_capture_timestamp(aio->base.format->frame_rate, delay, &shm->ts);

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

	rc = cras_alsa_get_avail_frames(aio->handle, aio->buffer_size, &frames);
	if (rc < 0)
		return rc;

	num_to_read = aio->cb_threshold;
	if (frames < aio->cb_threshold)
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
	remainder = frames - aio->cb_threshold;
	if (frames < aio->cb_threshold)
		remainder = 0;

	to_sleep = (aio->cb_threshold - remainder);
	if (aio->cb_threshold < remainder)
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
	int to_read, nread;
	int ret = 0;
	int err;

	/* Get the length of the message first */
	nread = read(aio->to_thread_fds[0], buf, sizeof(msg->length));
	if (nread < 0)
		return nread;
	if (msg->length > 256)
		return -ENOMEM;

	to_read = msg->length - nread;
	err = read(aio->to_thread_fds[0], &buf[0] + nread, to_read);
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
		err = write(aio->to_main_fds[1], &ret, sizeof(ret));
		if (err < 0)
			return err;
		terminate_pb_thread(aio);
		break;
	default:
		ret = -EINVAL;
		break;
	}

	err = write(aio->to_main_fds[1], &ret, sizeof(ret));
	if (err < 0)
		return err;
	return ret;
}

/* For playback, fill the audio buffer when needed, for capture, pull out
 * samples when they are ready.
 * This thread will attempt to run at a high priority to allow for low latency
 * streams.  This thread sleeps while alsa plays back or captures audio, it
 * will wake up as little as it can while avoiding xruns.  It can also be woken
 * by sending it a message using the "post_message_to_playback_thread" function.
 */
static void *alsa_io_thread(void *arg)
{
	struct alsa_io *aio = (struct alsa_io *)arg;
	struct timespec ts;
	fd_set poll_set;
	int msg_fd = aio->to_thread_fds[0];
	int err;

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

/* Write a message to the playback thread and wait for an ack, This keeps these
 * operations synchronous for the main server thread.  For instance when the
 * RM_STREAM message is sent, the stream can be deleted after the function
 * returns.  Making this synchronous also allows the thread to return an error
 * code that can be handled by the caller.
 */
static int post_message_to_playback_thread(struct alsa_io *aio,
					   struct cras_iodev_msg *msg)
{
	int rc, err;

	err = write(aio->to_thread_fds[1], msg, msg->length);
	if (err < 0)
		return err;
	err = read(aio->to_main_fds[0], &rc, sizeof(rc));
	if (err < 0)
		return err;

	return rc;
}

/* Add a stream to the output (called by iodev_list).
 * Args:
 *    iodev - a pointer to the alsa_io device.
 *    stream - the new stream to add.
 * Returns:
 *    zero on success negative error otherwise.
 */
static int add_stream(struct cras_iodev *iodev,
		      struct cras_rstream *stream)
{
	struct alsa_io *aio = (struct alsa_io *)iodev;
	struct cras_iodev_add_rm_stream_msg msg;

	assert(iodev && stream);

	msg.header.id = CRAS_IODEV_ADD_STREAM;
	msg.header.length = sizeof(struct cras_iodev_add_rm_stream_msg);
	msg.stream = stream;
	return post_message_to_playback_thread(aio, &msg.header);
}

/* Remove a stream from the output (called by iodev_list).
 * Args:
 *    iodev - a pointer to the alsa_io device.
 *    stream - the new stream to add.
 * Returns:
 *    zero on success negative error otherwise.
 */
static int rm_stream(struct cras_iodev *iodev,
		     struct cras_rstream *stream)
{
	struct alsa_io *aio = (struct alsa_io *)iodev;
	struct cras_iodev_add_rm_stream_msg msg;

	assert(iodev && stream);

	msg.header.id = CRAS_IODEV_RM_STREAM;
	msg.header.length = sizeof(struct cras_iodev_add_rm_stream_msg);
	msg.stream = stream;
	return post_message_to_playback_thread(aio, &msg.header);
}

/* Frees resources used by the alsa iodev.
 * Args:
 *    iodev - the iodev to free the resources from.
 */
static void free_alsa_iodev_resources(struct alsa_io *aio)
{
	if (aio->to_thread_fds[0] != -1) {
		close(aio->to_thread_fds[0]);
		close(aio->to_thread_fds[1]);
		aio->to_thread_fds[0] = -1;
		aio->to_thread_fds[1] = -1;
	}
	if (aio->to_main_fds[0] != -1) {
		close(aio->to_main_fds[0]);
		close(aio->to_main_fds[1]);
		aio->to_main_fds[0] = -1;
		aio->to_main_fds[1] = -1;
	}
	if (aio->mixer != NULL)
		cras_alsa_mixer_destroy(aio->mixer);
	free(aio->base.supported_rates);
	free(aio->base.supported_channel_counts);
	free(aio->dev);
}

/*
 * Exported Interface.
 */

struct cras_iodev *alsa_iodev_create(const char *dev,
				     struct cras_alsa_mixer *mixer,
				     enum CRAS_STREAM_DIRECTION direction)
{
	struct alsa_io *aio;
	struct cras_iodev *iodev;
	int err;

	if (dev == NULL)
		return NULL;

	aio = (struct alsa_io *)malloc(sizeof(*aio));
	if (!aio)
		return NULL;
	memset(aio, 0, sizeof(*aio));
	iodev = &aio->base;
	aio->dev = strdup(dev);
	if (aio->dev == NULL)
		goto cleanup_iodev;
	aio->to_thread_fds[0] = -1;
	aio->to_thread_fds[1] = -1;
	aio->to_main_fds[0] = -1;
	aio->to_main_fds[1] = -1;
	iodev->direction = direction;
	iodev->rm_stream = rm_stream;
	iodev->add_stream = add_stream;
	if (direction == CRAS_STREAM_INPUT) {
		aio->alsa_stream = SND_PCM_STREAM_CAPTURE;
		aio->alsa_cb = possibly_read_audio;
	} else {
		assert(direction == CRAS_STREAM_OUTPUT);
		aio->alsa_stream = SND_PCM_STREAM_PLAYBACK;
		aio->alsa_cb = possibly_fill_audio;
	}

	/* Two way pipes for communication with the device's audio thread. */
	err = pipe(aio->to_thread_fds);
	if (err < 0)
		goto cleanup_iodev;
	err = pipe(aio->to_main_fds);
	if (err < 0)
		goto cleanup_iodev;

	err = cras_alsa_fill_properties(aio->dev, aio->alsa_stream,
					&iodev->supported_rates,
					&iodev->supported_channel_counts);
	if (err < 0 || iodev->supported_rates[0] == 0 ||
	    iodev->supported_channel_counts[0] == 0)
		goto cleanup_iodev;

	aio->mixer = mixer;

	/* Start the device thread, it will block until a stream is added. */
	if (pthread_create(&aio->tid, NULL, alsa_io_thread, aio))
		goto cleanup_iodev;

	/* Finally add it to the approriate iodev list. */
	if (direction == CRAS_STREAM_INPUT)
		cras_iodev_list_add_input(&aio->base);
	else {
		assert(direction == CRAS_STREAM_OUTPUT);
		cras_iodev_list_add_output(&aio->base);
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
	struct cras_iodev_msg msg;
	int rc;

	msg.id = CRAS_IODEV_STOP;
	msg.length = sizeof(msg);
	post_message_to_playback_thread(aio, &msg);
	pthread_join(aio->tid, NULL);

	free_alsa_iodev_resources(aio);

	if (iodev->direction == CRAS_STREAM_INPUT)
		rc = cras_iodev_list_rm_input(iodev);
	else {
		assert(iodev->direction == CRAS_STREAM_OUTPUT);
		rc = cras_iodev_list_rm_output(iodev);
	}
	if (rc == 0)
		free(iodev);
}

