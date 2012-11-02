/* Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <pthread.h>
#include <stdlib.h>
#include <sys/time.h>
#include <syslog.h>
#include <time.h>

#include "cras_iodev.h"
#include "cras_rstream.h"
#include "cras_system_state.h"
#include "audio_thread.h"
#include "utlist.h"

/*
 * Exported Interface.
 */

/* Finds the supported sample rate that best suits the requested rate, "rrate".
 * Exact matches have highest priority, then integer multiples, then the default
 * rate for the device. */
static size_t get_best_rate(struct cras_iodev *iodev, size_t rrate)
{
	size_t i;
	size_t best;

	if (iodev->supported_rates[0] == 0) /* No rates supported */
		return 0;

	for (i = 0, best = 0; iodev->supported_rates[i] != 0; i++) {
		if (rrate == iodev->supported_rates[i])
			return rrate;
		if (best == 0 && (rrate % iodev->supported_rates[i] == 0 ||
				  iodev->supported_rates[i] % rrate == 0))
			best = iodev->supported_rates[i];
	}

	if (best)
		return best;
	return iodev->supported_rates[0];
}

/* Finds the best match for the channel count.  This will return an exact match
 * only, if there is no exact match, it falls back to the default channel count
 * for the device (The first in the list). */
static size_t get_best_channel_count(struct cras_iodev *iodev, size_t count)
{
	size_t i;

	assert(iodev->supported_channel_counts[0] != 0);

	for (i = 0; iodev->supported_channel_counts[i] != 0; i++) {
		if (iodev->supported_channel_counts[i] == count)
			return count;
	}
	return iodev->supported_channel_counts[0];
}

int cras_iodev_set_format(struct cras_iodev *iodev,
			  struct cras_audio_format *fmt)
{
	size_t actual_rate, actual_num_channels;
	const char *purpose;

	/* If this device isn't already using a format, try to match the one
	 * requested in "fmt". */
	if (iodev->format == NULL) {
		iodev->format = malloc(sizeof(struct cras_audio_format));
		if (!iodev->format)
			return -ENOMEM;
		*iodev->format = *fmt;

		if (iodev->update_supported_formats) {
			int rc = iodev->update_supported_formats(iodev);
			if (rc) {
				syslog(LOG_ERR, "Failed to update formats");
				return rc;
			}
		}


		actual_rate = get_best_rate(iodev, fmt->frame_rate);
		actual_num_channels = get_best_channel_count(iodev,
							     fmt->num_channels);
		if (actual_rate == 0 || actual_num_channels == 0) {
			/* No compatible frame rate found. */
			free(iodev->format);
			iodev->format = NULL;
			return -EINVAL;
		}
		iodev->format->frame_rate = actual_rate;
		iodev->format->num_channels = actual_num_channels;
		/* TODO(dgreid) - allow other formats. */
		iodev->format->format = SND_PCM_FORMAT_S16_LE;

		if (iodev->direction == CRAS_STREAM_OUTPUT)
			purpose = "playback";
		else
			purpose = "capture";
		iodev->dsp_context = cras_dsp_context_new(actual_num_channels,
							  actual_rate, purpose);
		if (iodev->dsp_context)
			cras_dsp_load_pipeline(iodev->dsp_context);
	}

	*fmt = *(iodev->format);
	return 0;
}

void cras_iodev_free_format(struct cras_iodev *iodev)
{
	if (iodev->format) {
		free(iodev->format);
		iodev->format = NULL;
	}
	if (iodev->dsp_context) {
		cras_dsp_context_free(iodev->dsp_context);
		iodev->dsp_context = NULL;
	}
}

void cras_iodev_fill_time_from_frames(size_t frames,
				      size_t frame_rate,
				      struct timespec *ts)
{
	uint64_t to_play_usec;

	ts->tv_sec = 0;
	/* adjust sleep time to target our callback threshold */
	to_play_usec = (uint64_t)frames * 1000000L / (uint64_t)frame_rate;

	while (to_play_usec > 1000000) {
		ts->tv_sec++;
		to_play_usec -= 1000000;
	}
	ts->tv_nsec = to_play_usec * 1000;
}

void cras_iodev_set_playback_timestamp(size_t frame_rate,
				       size_t frames,
				       struct timespec *ts)
{
	clock_gettime(CLOCK_MONOTONIC, ts);

	/* For playback, want now + samples left to be played.
	 * ts = time next written sample will be played to DAC,
	 */
	ts->tv_nsec += frames * 1000000000ULL / frame_rate;
	while (ts->tv_nsec > 1000000000ULL) {
		ts->tv_sec++;
		ts->tv_nsec -= 1000000000ULL;
	}
}

void cras_iodev_set_capture_timestamp(size_t frame_rate,
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

void cras_iodev_config_params(struct cras_iodev *iodev,
			      unsigned int buffer_size,
			      unsigned int cb_threshold)
{
	iodev->used_size = buffer_size;
	if (iodev->used_size > iodev->buffer_size)
		iodev->used_size = iodev->buffer_size;
	iodev->cb_threshold = cb_threshold;

	/* For output streams, callback when at most half way full. */
	if (iodev->direction == CRAS_STREAM_OUTPUT &&
	    iodev->cb_threshold > iodev->used_size / 2)
		iodev->cb_threshold = iodev->used_size / 2;

	syslog(LOG_DEBUG,
	       "used_size %u cb_threshold %u",
	       (unsigned)iodev->used_size,
	       (unsigned)iodev->cb_threshold);
}

void cras_iodev_plug_event(struct cras_iodev *iodev, int plugged)
{
	if (plugged)
		gettimeofday(&iodev->info.plugged_time, NULL);
	iodev->info.plugged = plugged;
}
