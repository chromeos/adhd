/* Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <alsa/asoundlib.h>
#include <alsa/use-case.h>
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
#include "cras_alsa_io.h"
#include "cras_alsa_jack.h"
#include "cras_alsa_mixer.h"
#include "cras_alsa_ucm.h"
#include "cras_config.h"
#include "cras_dsp.h"
#include "cras_dsp_pipeline.h"
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
 * on and off such as headphones or speakers.
 * Members:
 *    mixer_output - From cras_alsa_mixer.
 *    jack_curve - In absense of a mixer output, holds a volume curve to use
 *        when this jack is plugged.
 *    jack - The jack associated with the jack_curve (if it exists).
 *    plugged - true if the device is plugged.
 *    priority - higher is better.
 */
struct alsa_output_node {
	struct cras_alsa_mixer_output *mixer_output;
	struct cras_volume_curve *jack_curve;
	const struct cras_alsa_jack *jack;
	int plugged;
	unsigned priority;
	struct alsa_output_node *prev, *next;
};

struct alsa_input_node {
	struct mixer_volume_control* mixer_input;
	const struct cras_alsa_jack *jack;
	int plugged;
	struct alsa_input_node *prev, *next;
};

/* Child of cras_iodev, alsa_io handles ALSA interaction for sound devices.
 * base - The cras_iodev structure "base class".
 * dev - String that names this device (e.g. "hw:0,0").
 * device_index - ALSA index of device, Y in "hw:X:Y".
 * handle - Handle to the opened ALSA device.
 * num_underruns - Number of times we have run out of data (playback only).
 * alsa_stream - Playback or capture type.
 * mixer - Alsa mixer used to control volume and mute of the device.
 * output_nodes - Alsa mixer outputs (Only used for output devices).
 * active_output - The current node being used for playback.
 * jack_list - List of alsa jack controls for this device.
 * ucm - ALSA use case manager, if configuration is found.
 * alsa_cb - Callback to fill or read samples (depends on direction).
 * mmap_offset - offset returned from mmap_begin.
 */
struct alsa_io {
	struct cras_iodev base;
	char *dev;
	size_t device_index;
	snd_pcm_t *handle;
	unsigned int num_underruns;
	snd_pcm_stream_t alsa_stream;
	struct cras_alsa_mixer *mixer;
	struct alsa_output_node *output_nodes;
	struct alsa_output_node *active_output;
	struct alsa_input_node *input_nodes;
	struct alsa_input_node *active_input;
	struct cras_alsa_jack_list *jack_list;
	snd_use_case_mgr_t *ucm;
	snd_pcm_uframes_t mmap_offset;
	int (*alsa_cb)(struct cras_iodev *iodev, struct timespec *ts);
};

static int get_frames_queued(const struct cras_iodev *iodev)
{
	struct alsa_io *aio = (struct alsa_io *)iodev;
	int rc;
	snd_pcm_uframes_t frames;

	rc = cras_alsa_get_avail_frames(aio->handle,
					aio->base.buffer_size,
					&frames);
	if (rc < 0)
		return rc;

	if (iodev->direction == CRAS_STREAM_INPUT)
		return (int)frames;

	/* For output, return number of frames that are used. */
	return iodev->buffer_size - frames;
}

static int get_delay_frames(const struct cras_iodev *iodev)
{
	struct alsa_io *aio = (struct alsa_io *)iodev;
	snd_pcm_sframes_t delay;
	int rc;

	rc = cras_alsa_get_delay_frames(aio->handle,
					iodev->buffer_size,
					&delay);
	if (rc < 0)
		return rc;

	return (int)delay;
}

/* Close down alsa. This happens when all threads are removed or when there is
 * an error with the device.
 */
static int close_dev(struct cras_iodev *iodev)
{
	struct alsa_io *aio = (struct alsa_io *)iodev;
	if (!aio->handle)
		return 0;
	cras_alsa_pcm_drain(aio->handle);
	cras_alsa_pcm_close(aio->handle);
	aio->handle = NULL;
	cras_iodev_free_format(&aio->base);
	return 0;
}

/* Configure the alsa device we will use. */
static int open_dev(struct cras_iodev *iodev)
{
	struct alsa_io *aio = (struct alsa_io *)iodev;
	snd_pcm_t *handle;
	int rc;
	struct cras_rstream *stream;

	/* This is called after the first stream added so configure for it. */
	stream = iodev->streams->stream;
	if (iodev->format == NULL)
		return -EINVAL;
	cras_rstream_get_format(stream, iodev->format);
	/* TODO(dgreid) - allow more formats here. */
	iodev->format->format = SND_PCM_FORMAT_S16_LE;
	aio->num_underruns = 0;
	aio->base.sleep_correction_frames = 0;

	syslog(LOG_DEBUG, "Configure alsa device %s rate %zuHz, %zu channels",
	       aio->dev, iodev->format->frame_rate,
	       iodev->format->num_channels);
	handle = 0; /* Avoid unused warning. */
	rc = cras_alsa_pcm_open(&handle, aio->dev, aio->alsa_stream);
	if (rc < 0)
		return rc;

	rc = cras_alsa_set_hwparams(handle, iodev->format,
				    &iodev->buffer_size);
	if (rc < 0) {
		cras_alsa_pcm_close(handle);
		return rc;
	}

	/* Set minimum number of available frames. */
	if (iodev->used_size > iodev->buffer_size)
		iodev->used_size = iodev->buffer_size;

	/* Configure software params. */
	rc = cras_alsa_set_swparams(handle);
	if (rc < 0) {
		cras_alsa_pcm_close(handle);
		return rc;
	}

	aio->handle = handle;

	/* Capture starts right away, playback will wait for samples. */
	if (aio->alsa_stream == SND_PCM_STREAM_CAPTURE)
		cras_alsa_pcm_start(aio->handle);

	return 0;
}

static int get_buffer(struct cras_iodev *iodev, uint8_t **dst, unsigned *frames)
{
	struct alsa_io *aio = (struct alsa_io *)iodev;
	snd_pcm_uframes_t nframes = *frames;
	int rc;

	aio->mmap_offset = 0;

	rc = cras_alsa_mmap_begin(aio->handle,
				  cras_get_format_bytes(iodev->format),
				  dst,
				  &aio->mmap_offset,
				  &nframes,
				  &aio->num_underruns);

	*frames = nframes;

	return rc;
}

static int put_buffer(struct cras_iodev *iodev, unsigned nwritten)
{
	struct alsa_io *aio = (struct alsa_io *)iodev;

	return cras_alsa_mmap_commit(aio->handle,
				     aio->mmap_offset,
				     nwritten,
				     &aio->num_underruns);
}

/* Gets the curve for the active output. */
static const struct cras_volume_curve *get_curve_for_active_output(
		const struct alsa_io *aio)
{
	if (aio->active_output &&
	    aio->active_output->mixer_output &&
	    aio->active_output->mixer_output->volume_curve)
		return aio->active_output->mixer_output->volume_curve;
	else if (aio->active_output && aio->active_output->jack_curve)
		return aio->active_output->jack_curve;
	return cras_alsa_mixer_default_volume_curve(aio->mixer);
}

/* Informs the system of the volume limits for this device. */
static void set_alsa_volume_limits(struct alsa_io *aio)
{
	const struct cras_volume_curve *curve;

	/* Only set the limits if the dev is active. */
	if (!cras_iodev_streams_attached(&aio->base))
		return;

	curve = get_curve_for_active_output(aio);
	cras_system_set_volume_limits(
			curve->get_dBFS(curve, 1), /* min */
			curve->get_dBFS(curve, CRAS_MAX_SYSTEM_VOLUME));
}

/* Sets the alsa mute state for this iodev. */
static void set_alsa_mute(const struct alsa_io *aio, int muted)
{
	if (!cras_iodev_streams_attached(&aio->base))
		return;

	cras_alsa_mixer_set_mute(
		aio->mixer,
		muted,
		aio->active_output ?
			aio->active_output->mixer_output : NULL);
}

/* Sets the volume of the playback device to the specified level. Receives a
 * volume index from the system settings, ranging from 0 to 100, converts it to
 * dB using the volume curve, and sends the dB value to alsa. Handles mute and
 * unmute, including muting when volume is zero. */
static void set_alsa_volume(struct cras_iodev *iodev)
{
	const struct alsa_io *aio = (const struct alsa_io *)iodev;
	const struct cras_volume_curve *curve;
	size_t volume;
	int mute;

	assert(aio);
	if (aio->mixer == NULL)
		return;

	/* Only set the volume if the dev is active. */
	if (!cras_iodev_streams_attached(&aio->base))
		return;

	volume = cras_system_get_volume();
	mute = cras_system_get_mute();
	curve = get_curve_for_active_output(aio);
	if (curve == NULL)
		return;
	cras_alsa_mixer_set_dBFS(
		aio->mixer,
		curve->get_dBFS(curve, volume),
		aio->active_output ?
			aio->active_output->mixer_output : NULL);
	/* Mute for zero. */
	set_alsa_mute(aio, mute || (volume == 0));
}

/* Sets the capture gain to the current system input gain level, given in dBFS.
 * Set mute based on the system mute state.  This gain can be positive or
 * negative and might be adjusted often if and app is running an AGC. */
static void set_alsa_capture_gain(struct cras_iodev *iodev)
{
	const struct alsa_io *aio = (const struct alsa_io *)iodev;

	assert(aio);
	if (aio->mixer == NULL)
		return;

	/* Only set the volume if the dev is active. */
	if (!cras_iodev_streams_attached(&aio->base))
		return;

	cras_alsa_mixer_set_capture_dBFS(
			aio->mixer,
			cras_system_get_capture_gain(),
			aio->active_input ?
				aio->active_input->mixer_input : NULL);
	cras_alsa_mixer_set_capture_mute(aio->mixer,
					 cras_system_get_capture_mute());
}

/* Initializes the device settings and registers for callbacks when system
 * settings have been changed.
 */
static void init_device_settings(struct alsa_io *aio)
{
	/* Register for volume/mute callback and set initial volume/mute for
	 * the device. */
	if (aio->base.direction == CRAS_STREAM_OUTPUT) {
		set_alsa_volume_limits(aio);
		set_alsa_volume(&aio->base);
	} else {
		struct mixer_volume_control *mixer_input = NULL;
		if (aio->active_input)
			mixer_input = aio->active_input->mixer_input;
		cras_system_set_capture_gain_limits(
			cras_alsa_mixer_get_minimum_capture_gain(aio->mixer,
								 mixer_input),
			cras_alsa_mixer_get_maximum_capture_gain(aio->mixer,
								 mixer_input));
		set_alsa_capture_gain(&aio->base);
	}
}

/*
 * Alsa I/O thread functions.
 *    These functions are all run from the audio thread.
 */

/* Handles the rm_stream message from the main thread.
 * If this is the last stream to be removed then stop the
 * audio thread and free the resources. */
static int thread_remove_stream(struct cras_iodev *iodev,
				struct cras_rstream *stream)
{
	struct alsa_io *aio = (struct alsa_io *)iodev;
	int rc;

	rc = cras_iodev_delete_stream(&aio->base, stream);
	if (rc != 0)
		return rc;

	if (!cras_iodev_streams_attached(&aio->base)) {
		/* No more streams, close alsa dev. */
		close_dev(&aio->base);
	} else {
		cras_iodev_config_params_for_streams(&aio->base);
		syslog(LOG_DEBUG,
		       "used_size %u cb_threshold %u",
		       (unsigned)aio->base.used_size,
		       (unsigned)aio->base.cb_threshold);
	}

	cras_rstream_set_iodev(stream, NULL);
	return 0;
}

/* Handles the add_stream message from the main thread. */
static int thread_add_stream(struct cras_iodev *iodev,
			     struct cras_rstream *stream)
{
	struct alsa_io *aio = (struct alsa_io *)iodev;
	int rc;

	/* Only allow one capture stream to attach. */
	if (iodev->direction == CRAS_STREAM_INPUT &&
	    iodev->streams != NULL)
		return -EBUSY;

	rc = cras_iodev_append_stream(iodev, stream);
	if (rc < 0)
		return rc;

	/* If not already, open alsa. */
	if (aio->handle == NULL) {
		init_device_settings(aio);

		rc = open_dev(iodev);
		if (rc < 0)
			syslog(LOG_ERR, "Failed to open %s", aio->dev);
	}

	cras_iodev_config_params_for_streams(iodev);
	return 0;
}

/* Reads any pending audio message from the socket. */
static void flush_old_aud_messages(struct cras_audio_shm *shm, int fd)
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
			cras_shm_set_callback_pending(shm, 0);
		}
	} while (err > 0);
}

/* Asks any streams with room for more data. Sets the timestamp for all streams.
 * Args:
 *    iodev - The iodev containing the streams to fetch from.
 *    fetch_size - How much to fetch.
 *    alsa_delay - How much latency is queued to alsa(frames)
 * Returns:
 *    0 on success, negative error on failure. If failed, can assume that all
 *    streams have been removed from the device.
 */
static int fetch_and_set_timestamp(struct cras_iodev *iodev, size_t fetch_size,
				   size_t alsa_delay)
{
	size_t fr_rate, frames_in_buff;
	struct cras_io_stream *curr, *tmp;
	int rc;

	fr_rate = iodev->format->frame_rate;

	DL_FOREACH_SAFE(iodev->streams, curr, tmp) {
		if (cras_shm_callback_pending(curr->shm))
			flush_old_aud_messages(curr->shm, curr->fd);

		frames_in_buff = cras_shm_get_frames(curr->shm);

		cras_iodev_set_playback_timestamp(fr_rate,
						  frames_in_buff + alsa_delay,
						  &curr->shm->area->ts);

		/* If we already have enough data, don't poll this stream. */
		if (frames_in_buff >= fetch_size)
			continue;

		if (!cras_shm_callback_pending(curr->shm) &&
		    cras_shm_is_buffer_available(curr->shm)) {
			rc = cras_rstream_request_audio(curr->stream,
							fetch_size);
			if (rc < 0) {
				thread_remove_stream(iodev, curr->stream);
				/* If this failed and was the last stream,
				 * return, otherwise, on to the next one */
				if (!cras_iodev_streams_attached(iodev))
					return -EIO;
				continue;
			}
			cras_shm_set_callback_pending(curr->shm, 1);
		}
	}

	return 0;
}

/* Fill the buffer with samples from the attached streams.
 * Args:
 *    iodev - The device to write new samples to.
 *    dst - The buffer to put the samples in (returned from snd_pcm_mmap_begin)
 *    level - The number of frames still in alsa buffer.
 *    write_limit - The maximum number of frames to write to dst.
 *
 * Returns:
 *    The number of frames rendered on success, a negative error code otherwise.
 *    This number of frames is the minimum of the amount of frames each stream
 *    could provide which is the maximum that can currently be rendered.
 */
static int write_streams(struct cras_iodev *iodev, uint8_t *dst, size_t level,
			 size_t write_limit)
{
	struct cras_io_stream *curr, *tmp;
	struct timeval to;
	fd_set poll_set, this_set;
	size_t streams_wait, num_mixed;
	int max_fd;
	int rc;

	/* Timeout on reading before we under-run. Leaving time to mix. */
	to.tv_sec = 0;
	to.tv_usec = (level * 1000000 / iodev->format->frame_rate);
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
	DL_FOREACH(iodev->streams, curr) {
		curr->mixed = 0;
		if (cras_shm_get_frames(curr->shm) < write_limit &&
		    cras_shm_callback_pending(curr->shm)) {
			/* Not enough to mix this call, wait for a response. */
			streams_wait++;
			FD_SET(curr->fd, &poll_set);
			if (curr->fd > max_fd)
				max_fd = curr->fd;
		} else {
			curr->mixed = cras_mix_add_stream(
				curr->shm,
				iodev->format->num_channels,
				dst, &write_limit, &num_mixed);
		}
	}

	/* Wait until all polled clients reply, or a timeout. */
	while (streams_wait > 0) {
		this_set = poll_set;
		rc = select(max_fd + 1, &this_set, NULL, NULL, &to);
		if (rc <= 0) {
			/* Timeout */
			DL_FOREACH(iodev->streams, curr) {
				if (cras_shm_callback_pending(curr->shm) &&
				    FD_ISSET(curr->fd, &poll_set))
					cras_shm_inc_cb_timeouts(curr->shm);
			}
			break;
		}
		DL_FOREACH_SAFE(iodev->streams, curr, tmp) {
			if (!FD_ISSET(curr->fd, &this_set))
				continue;

			FD_CLR(curr->fd, &poll_set);
			streams_wait--;
			cras_shm_set_callback_pending(curr->shm, 0);
			rc = cras_rstream_get_audio_request_reply(curr->stream);
			if (rc < 0) {
				thread_remove_stream(iodev, curr->stream);
				if (!cras_iodev_streams_attached(iodev))
					return -EIO;
				continue;
			}
			if (curr->mixed)
				continue;
			curr->mixed = cras_mix_add_stream(
				curr->shm,
				iodev->format->num_channels,
				dst, &write_limit, &num_mixed);
		}
	}

	if (num_mixed == 0)
		return num_mixed;

	/* For all streams rendered, mark the data used as read. */
	DL_FOREACH(iodev->streams, curr)
		if (curr->mixed)
			cras_shm_buffer_read(curr->shm, write_limit);

	return write_limit;
}

static void apply_dsp_pipeline(struct pipeline *pipeline, size_t channels,
			       uint8_t *buf, size_t frames)
{
	size_t chunk;
	size_t i, j;
	int16_t *target, *target_ptr;
	float *source[channels], *sink[channels];
	float *source_ptr[channels], *sink_ptr[channels];

	if (!pipeline || frames == 0)
		return;

	target = (int16_t *)buf;

	/* get pointers to source and sink buffers */
	for (i = 0; i < channels; i++) {
		source[i] = cras_dsp_pipeline_get_source_buffer(pipeline, i);
		sink[i] = cras_dsp_pipeline_get_sink_buffer(pipeline, i);
	}

	/* process at most DSP_BUFFER_SIZE frames each loop */
	while (frames > 0) {
		chunk = min(frames, (size_t)DSP_BUFFER_SIZE);

		/* deinterleave and convert to float */
		target_ptr = target;
		for (i = 0; i < channels; i++)
			source_ptr[i] = source[i];
		for (i = 0; i < chunk; i++) {
			for (j = 0; j < channels; j++)
				*(source_ptr[j]++) = *target_ptr++ / 32768.0f;
		}

		cras_dsp_pipeline_run(pipeline, chunk);

		/* interleave and convert back to int16_t */
		target_ptr = target;
		for (i = 0; i < channels; i++)
			sink_ptr[i] = sink[i];
		for (i = 0; i < chunk; i++) {
			for (j = 0; j < channels; j++) {
				float f = *(sink_ptr[j]++) * 32768.0f;
				int16_t i16;
				if (f > 32767)
					i16 = 32767;
				else if (f < -32768)
					i16 = -32768;
				else
					i16 = (int16_t) (f + 0.5f);
				*target_ptr++ = i16;
			}
		}

		target += chunk * channels;
		frames -= chunk;
	}
}

static void apply_dsp(struct cras_iodev *iodev, uint8_t *buf, size_t frames)
{
	struct cras_dsp_context *ctx;
	struct pipeline *pipeline;

	ctx = iodev->dsp_context;
	if (!ctx)
		return;

	pipeline = cras_dsp_get_pipeline(ctx);
	if (!pipeline)
		return;

	apply_dsp_pipeline(pipeline, iodev->format->num_channels, buf,
			   frames);

	cras_dsp_put_pipeline(ctx);
}

static inline int have_enough_frames(const struct cras_iodev *iodev,
				     unsigned int frames)
{
	if (iodev->direction == CRAS_STREAM_OUTPUT)
		return frames <= (iodev->cb_threshold + SLEEP_FUZZ_FRAMES);

	/* Input or unified. */
	return frames >= iodev->cb_threshold;
}

static int dev_running(const struct cras_iodev *iodev)
{
	struct alsa_io *aio = (struct alsa_io *)iodev;
	snd_pcm_t *handle = aio->handle;
	int rc;

	if (snd_pcm_state(handle) == SND_PCM_STATE_RUNNING)
		return 0;

	if (snd_pcm_state(handle) == SND_PCM_STATE_SUSPENDED) {
		rc = cras_alsa_attempt_resume(handle);
		if (rc < 0)
			return rc;
	} else {
		rc = cras_alsa_pcm_start(handle);
		if (rc < 0) {
			syslog(LOG_ERR, "Start error: %s", snd_strerror(rc));
			return rc;
		}
	}

	return 0;
}

/* Check if we should get more samples for playback from the source streams. If
 * more data is needed by the output, fetch and render it.
 * Args:
 *    ts - how long to sleep before calling this again.
 * Returns:
 *    0 if successful, otherwise a negative error.  If an error occurs you can
 *    assume that the pcm is no longer functional.
 */
static int possibly_fill_audio(struct cras_iodev *iodev,
			       struct timespec *ts)
{
	unsigned int frames, used, fr_to_req;
	snd_pcm_sframes_t written, delay;
	snd_pcm_uframes_t total_written = 0;
	int rc;
	uint8_t *dst = NULL;
	uint64_t to_sleep;

	ts->tv_sec = ts->tv_nsec = 0;

	rc = iodev->frames_queued(iodev);
	if (rc < 0)
		return rc;
	used = rc;

	/* Make sure we should actually be awake right now (or close enough) */
	if (!have_enough_frames(iodev, used)) {
		/* Check if the pcm is still running. */
		rc = dev_running(iodev);
		if (rc < 0)
			return rc;
		/* Increase sleep correction factor when waking up too early. */
		iodev->sleep_correction_frames++;
		goto not_enough;
	}

	/* check the current delay through alsa */
	rc = iodev->delay_frames(iodev);
	if (rc < 0)
		return rc;
	delay = rc;

	/* Request data from streams that need more */
	fr_to_req = iodev->used_size - used;
	rc = fetch_and_set_timestamp(iodev, fr_to_req, delay);
	if (rc < 0)
		return rc;

	/* Have to loop writing to alsa, will be at most 2 loops, this only
	 * happens when the circular buffer is at the end and returns us a
	 * partial area to write to from mmap_begin */
	while (total_written < fr_to_req) {
		frames = fr_to_req - total_written;
		rc = iodev->get_buffer(iodev, &dst, &frames);
		if (rc < 0)
			return rc;

		written = write_streams(iodev, dst, used + total_written,
					frames);
		if (written < 0) /* pcm has been closed */
			return (int)written;

		if ((unsigned)written < frames)
			/* Got all the samples from client that we can, but it
			 * won't fill the request. */
			fr_to_req = 0; /* break out after committing samples */

		apply_dsp(iodev, dst, written);
		rc = iodev->put_buffer(iodev, written);
		if (rc < 0)
			return rc;
		total_written += written;
	}

	/* If we haven't started alsa and we wrote samples, then start it up. */
	if (total_written) {
		rc = iodev->dev_running(iodev);
		if (rc < 0)
			return rc;
	}

not_enough:
	/* Set the sleep time based on how much is left to play */
	to_sleep = cras_iodev_sleep_frames(iodev, total_written + used) +
		   iodev->sleep_correction_frames;
	cras_iodev_fill_time_from_frames(to_sleep,
					 iodev->format->frame_rate,
					 ts);

	return 0;
}

/* Pass captured samples to the client.
 * Args:
 *    src - the memory area containing the captured samples.
 *    count - the number of frames captured = buffer_frames.
 */
static void read_streams(struct cras_iodev *iodev,
			 const uint8_t *src,
			 size_t count)
{
	struct cras_io_stream *streams;
	struct cras_audio_shm *shm;
	unsigned write_limit;
	uint8_t *dst;

	streams = iodev->streams;
	if (!streams)
		return; /* Nowhere to put samples. */

	shm = streams->shm;

	dst = cras_shm_get_writeable_frames(shm, &write_limit);
	count = min(count, write_limit);
	memcpy(dst, src, count * cras_shm_frame_bytes(shm));
	cras_shm_buffer_written(shm, count);
}

/* The capture path equivalent of possibly_fill_audio
 * Read audio cb_threshold samples at a time, then sleep, until next
 * cb_samples have been received.
 * Args:
 *    ts - fills with how long to sleep before calling this again.
 * Returns:
 *    0 unless there is an error talking to alsa or the client.
 */
static int possibly_read_audio(struct cras_iodev *iodev,
			       struct timespec *ts)
{
	/* Sleep a few extra frames to make sure that the samples are ready. */
	static const size_t REMAINING_FRAMES_TARGET = 16;

	snd_pcm_uframes_t used, num_to_read, remainder;
	snd_pcm_sframes_t delay;
	struct cras_audio_shm *shm;
	int rc;
	uint64_t to_sleep;
	uint8_t *src;
	uint8_t *dst = NULL;
	unsigned int write_limit = 0;
	unsigned int nread;

	ts->tv_sec = ts->tv_nsec = 0;
	num_to_read = iodev->cb_threshold;

	rc = iodev->frames_queued(iodev);
	if (rc < 0)
		return rc;
	used = rc;

	if (!have_enough_frames(iodev, used)) {
		to_sleep = num_to_read - used;
		/* Increase sleep correction factor when waking up too early. */
		iodev->sleep_correction_frames++;
		goto dont_read;
	}

	rc = iodev->delay_frames(iodev);
	if (rc < 0)
		return rc;
	delay = rc;

	if (iodev->streams) {
		cras_shm_check_write_overrun(iodev->streams->shm);
		shm = iodev->streams->shm;
		cras_iodev_set_capture_timestamp(iodev->format->frame_rate,
						 delay,
						 &shm->area->ts);
		dst = cras_shm_get_writeable_frames(shm, &write_limit);
	}

	remainder = num_to_read;
	while (remainder > 0) {
		nread = remainder;
		rc = iodev->get_buffer(iodev, &src, &nread);
		if (rc < 0 || nread == 0)
			return rc;

		read_streams(iodev, src, nread);

		rc = iodev->put_buffer(iodev, nread);
		if (rc < 0)
			return rc;
		remainder -= nread;
	}

	if (iodev->streams) {
		apply_dsp(iodev, dst, min(num_to_read, write_limit));
		cras_shm_buffer_write_complete(iodev->streams->shm);

		/* Tell the client that samples are ready.  This assumes only
		 * one capture client at a time. */
		rc = cras_rstream_audio_ready(iodev->streams->stream,
					      num_to_read);
		if (rc < 0) {
			thread_remove_stream(iodev,
					     iodev->streams->stream);
			return rc;
		}
	}

	/* Adjust sleep time to target our callback threshold. */
	remainder = used - num_to_read;
	to_sleep = num_to_read - remainder;
	/* If there are more remaining frames than targeted, decrease the sleep
	 * time.  If less, increase. */
	if (remainder != REMAINING_FRAMES_TARGET)
		iodev->sleep_correction_frames +=
			(remainder > REMAINING_FRAMES_TARGET) ? -1 : 1;

dont_read:
	to_sleep += REMAINING_FRAMES_TARGET + iodev->sleep_correction_frames;
	cras_iodev_fill_time_from_frames(to_sleep,
					 iodev->format->frame_rate,
					 ts);

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
		ret = thread_add_stream(&aio->base, amsg->stream);
		break;
	}
	case CRAS_IODEV_RM_STREAM: {
		struct cras_iodev_add_rm_stream_msg *rmsg;
		const struct cras_audio_shm *shm;

		rmsg = (struct cras_iodev_add_rm_stream_msg *)msg;
		shm = cras_rstream_get_shm(rmsg->stream);
		if (shm != NULL) {
			syslog(LOG_DEBUG, "cb_timeouts:%u",
			       cras_shm_num_cb_timeouts(shm));
			syslog(LOG_DEBUG, "overruns:%u",
			       cras_shm_num_overruns(shm));
		}
		ret = thread_remove_stream(&aio->base, rmsg->stream);
		if (ret < 0)
			syslog(LOG_INFO, "Failed to remove the stream");
		syslog(LOG_DEBUG, "underruns:%u", aio->num_underruns);
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
			err = aio->alsa_cb(&aio->base, &ts);
			if (err < 0) {
				syslog(LOG_INFO, "alsa cb error %d", err);
				close_dev(&aio->base);
			}
			wait_ts = &ts;
		}

		FD_ZERO(&poll_set);
		FD_SET(msg_fd, &poll_set);
		err = pselect(msg_fd + 1, &poll_set, NULL, NULL, wait_ts, NULL);
		if (err > 0 && FD_ISSET(msg_fd, &poll_set)) {
			err = handle_playback_thread_message(aio);
			if (err < 0)
				syslog(LOG_INFO, "handle message %d", err);
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
	struct alsa_input_node *input, *tmp_input;

	cras_iodev_deinit(&aio->base);
	free(aio->base.supported_rates);
	free(aio->base.supported_channel_counts);
	DL_FOREACH_SAFE(aio->output_nodes, output, tmp) {
		DL_DELETE(aio->output_nodes, output);
		cras_volume_curve_destroy(output->jack_curve);
		free(output);
	}
	DL_FOREACH_SAFE(aio->input_nodes, input, tmp_input) {
		DL_DELETE(aio->input_nodes, input);
		free(input);
	}
	free(aio->dev);
}

/* Sets the initial plugged state and priority of an output node based on its
 * name.
 */
static void set_output_prio(struct alsa_output_node *node, const char *name)
{
	static const struct {
		const char *name;
		int priority;
		int initial_plugged;
	} prios[] = {
		{ "Speaker", 1, 1 },
		{ "Headphone", 3, 0 },
		{ "HDMI", 2, 0 },
	};
	unsigned i;

	for (i = 0; i < ARRAY_SIZE(prios); i++)
		if (!strcmp(name, prios[i].name)) {
			node->priority = prios[i].priority;
			node->plugged = prios[i].initial_plugged;
			break;
		}
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

	if (output->mixer_output)
		set_output_prio(
			output,
			cras_alsa_mixer_get_output_name(output->mixer_output));

	DL_APPEND(aio->output_nodes, output);
}

/* Finds the output node associated with the jack. Returns NULL if not found. */
static struct alsa_output_node *get_output_node_from_jack(
		struct alsa_io *aio, const struct cras_alsa_jack *jack)
{
	struct cras_alsa_mixer_output *mixer_output;
	struct alsa_output_node *node = NULL;

	mixer_output = cras_alsa_jack_get_mixer_output(jack);
	if (mixer_output == NULL) {
		/* no mixer output, search by node. */
		DL_SEARCH_SCALAR(aio->output_nodes, node, jack, jack);
		return node;
	}

	DL_SEARCH_SCALAR(aio->output_nodes, node, mixer_output, mixer_output);
	return node;
}

static struct alsa_input_node *get_input_node_from_jack(
		struct alsa_io *aio, const struct cras_alsa_jack *jack)
{
	struct mixer_volume_control *mixer_input =
			cras_alsa_jack_get_mixer_input(jack);
	struct alsa_input_node *node = NULL;

	if (mixer_input == NULL) {
		DL_SEARCH_SCALAR(aio->input_nodes, node, jack, jack);
		return node;
	}

	DL_SEARCH_SCALAR(aio->input_nodes, node, mixer_input, mixer_input);
	return node;
}

/* Find the node with highest priority that is plugged in. */
static struct alsa_output_node *get_best_output_node(const struct alsa_io *aio)
{
	struct alsa_output_node *output;
	struct alsa_output_node *best = NULL;

	DL_FOREACH(aio->output_nodes, output)
		if (output->plugged &&
		    (!best || (output->priority > best->priority)))
			best = output;

	/* If nothing is plugged, take the first entry. */
	if (!best)
		return aio->output_nodes;

	return best;
}

/* Callback that is called when an output jack is plugged or unplugged. */
static void jack_output_plug_event(const struct cras_alsa_jack *jack,
				    int plugged,
				    void *arg)
{
	struct alsa_io *aio;
	struct alsa_output_node *node, *best_node;

	if (arg == NULL)
		return;

	aio = (struct alsa_io *)arg;
	node = get_output_node_from_jack(aio, jack);

	/* If there isn't a node for this jack, create one. */
	if (node == NULL) {
		node = (struct alsa_output_node *)calloc(1, sizeof(*node));
		if (node == NULL) {
			syslog(LOG_ERR, "Out of memory creating jack node.");
			return;
		}
		node->jack_curve = cras_alsa_mixer_create_volume_curve_for_name(
				aio->mixer, cras_alsa_jack_get_name(jack));
		node->jack = jack;
		DL_APPEND(aio->output_nodes, node);
	}

	cras_iodev_plug_event(&aio->base, plugged);

	/* If the jack has a ucm device, set that. */
	cras_alsa_jack_enable_ucm(jack, plugged);

	node->plugged = plugged;

	best_node = get_best_output_node(aio);

	/* If thie node is associated with mixer output, set that output
	 * as active and set up the mixer, otherwise just set the node
	 * as active and set the volume curve. */
	if (best_node->mixer_output != NULL) {
		alsa_iodev_set_active_output(&aio->base,
					     best_node->mixer_output);
	} else {
		aio->active_output = best_node;
		init_device_settings(aio);
	}

	cras_iodev_move_stream_type_top_prio(CRAS_STREAM_TYPE_DEFAULT,
					     aio->base.direction);
}

/* Callback that is called when an input jack is plugged or unplugged. */
static void jack_input_plug_event(const struct cras_alsa_jack *jack,
				  int plugged,
				  void *arg)
{
	struct alsa_io *aio;
	struct alsa_input_node *node;

	if (arg == NULL)
		return;
	aio = (struct alsa_io *)arg;
	node = get_input_node_from_jack(aio, jack);
	if (node == NULL) {
		node = (struct alsa_input_node *)calloc(1, sizeof(*node));
		if (node == NULL) {
			syslog(LOG_ERR, "Out of memory creating jack node.");
			return;
		}
		node->jack = jack;
		node->mixer_input = cras_alsa_jack_get_mixer_input(jack);

		DL_APPEND(aio->input_nodes, node);
	}

	cras_iodev_plug_event(&aio->base, plugged);

	/* If the jack has a ucm device, set that. */
	cras_alsa_jack_enable_ucm(jack, plugged);

	node->plugged = plugged;

	/* Choose the first plugged node. */
	DL_SEARCH_SCALAR(aio->input_nodes, node, plugged, 1);
	aio->active_input = node;

	init_device_settings(aio);

	syslog(LOG_DEBUG, "Move input streams due to plug event.");
	cras_iodev_move_stream_type_top_prio(CRAS_STREAM_TYPE_DEFAULT,
					     aio->base.direction);
}

/* Sets the name of the given iodev, using the name and index of the card
 * combined with the device index and direction */
static void set_iodev_name(struct cras_iodev *dev,
			   const char *card_name,
			   const char *dev_name,
			   size_t card_index,
			   size_t device_index)
{
	snprintf(dev->info.name,
		 sizeof(dev->info.name),
		 "%s: %s:%zu,%zu",
		 card_name,
		 dev_name,
		 card_index,
		 device_index);
	dev->info.name[ARRAY_SIZE(dev->info.name) - 1] = '\0';
	syslog(LOG_DEBUG, "Add device name=%s", dev->info.name);
}

/* Updates the supported sample rates and channel counts. */
static int update_supported_formats(struct cras_iodev *iodev)
{
	struct alsa_io *aio = (struct alsa_io *)iodev;
	int err;

	free(iodev->supported_rates);
	iodev->supported_rates = NULL;
	free(iodev->supported_channel_counts);
	iodev->supported_channel_counts = NULL;

	err = cras_alsa_fill_properties(aio->dev, aio->alsa_stream,
					&iodev->supported_rates,
					&iodev->supported_channel_counts);
	return err;
}

static void set_as_default(struct cras_iodev *iodev) {
	struct alsa_io *aio = (struct alsa_io *)iodev;
	init_device_settings(aio);
}

/*
 * Exported Interface.
 */

struct cras_iodev *alsa_iodev_create(size_t card_index,
				     const char *card_name,
				     size_t device_index,
				     const char *dev_name,
				     struct cras_alsa_mixer *mixer,
				     snd_use_case_mgr_t *ucm,
				     size_t prio,
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
	aio->handle = NULL;
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
		aio->base.set_capture_gain = set_alsa_capture_gain;
		aio->base.set_capture_mute = set_alsa_capture_gain;
	} else {
		assert(direction == CRAS_STREAM_OUTPUT);
		aio->alsa_stream = SND_PCM_STREAM_PLAYBACK;
		aio->alsa_cb = possibly_fill_audio;
		aio->base.set_volume = set_alsa_volume;
		aio->base.set_mute = set_alsa_volume;
	}
	iodev->update_supported_formats = update_supported_formats;
	iodev->set_as_default = set_as_default;
	iodev->frames_queued = get_frames_queued;
	iodev->delay_frames = get_delay_frames;
	iodev->get_buffer = get_buffer;
	iodev->put_buffer = put_buffer;
	iodev->dev_running = dev_running;

	err = cras_alsa_fill_properties(aio->dev, aio->alsa_stream,
					&iodev->supported_rates,
					&iodev->supported_channel_counts);
	if (err < 0 || iodev->supported_rates[0] == 0 ||
	    iodev->supported_channel_counts[0] == 0) {
		syslog(LOG_ERR, "cras_alsa_fill_properties: %s", strerror(err));
		goto cleanup_iodev;
	}

	aio->mixer = mixer;
	aio->ucm = ucm;
	set_iodev_name(iodev, card_name, dev_name, card_index, device_index);
	iodev->info.priority = prio;

	if (direction == CRAS_STREAM_INPUT)
		cras_iodev_list_add_input(&aio->base);
	else {
		assert(direction == CRAS_STREAM_OUTPUT);

		/* Check for outputs, sudh as Headphone and Speaker. */
		cras_alsa_mixer_list_outputs(mixer, device_index,
					     new_output, aio);
		/* If we don't have separate outputs just make a default one. */
		if (aio->output_nodes == NULL)
			new_output(NULL, aio);
		alsa_iodev_set_active_output(&aio->base,
					     aio->output_nodes->mixer_output);

		/* Add to the output list. */
		cras_iodev_list_add_output(&aio->base);
	}

	/* Find any jack controls for this device. */
	aio->jack_list = cras_alsa_jack_list_create(
			card_index,
			card_name,
			device_index,
			mixer,
			ucm,
			direction,
			direction == CRAS_STREAM_OUTPUT ?
				     jack_output_plug_event :
				     jack_input_plug_event,
			aio);

	/* Create output nodes for jacks that aren't associated with an already
	 * existing node.  Get an initial read of the jacks for this device. */
	cras_alsa_jack_list_report(aio->jack_list);

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

	cras_alsa_jack_list_destroy(aio->jack_list);
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

	set_alsa_mute(aio, 1);
	/* Unmute the active input, mute all others. */
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
		syslog(LOG_WARNING, "Trying to switch to non-existant output.");
		return -EINVAL;
	}
	/* Setting the volume will also unmute if the system isn't muted. */
	init_device_settings(aio);
	return 0;
}
