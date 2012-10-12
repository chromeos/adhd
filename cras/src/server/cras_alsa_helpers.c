/* Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <alsa/asoundlib.h>
#include <limits.h>
#include <stdlib.h>
#include <syslog.h>

#include "cras_alsa_helpers.h"
#include "cras_types.h"
#include "cras_util.h"

/* Chances to give mmap_begin to work. */
static const size_t MAX_MMAP_BEGIN_ATTEMPTS = 3;
/* Time to sleep between resume attempts. */
static const size_t ALSA_SUSPENDED_SLEEP_TIME_US = 250000;

/* What rates should we check for on this dev?
 * Listed in order of preference. 0 terminalted. */
static const size_t test_sample_rates[] = {
	44100,
	48000,
	32000,
	96000,
	22050,
	8000,
	4000,
	192000,
	0
};

/* What channel counts shoud be checked on this dev?
 * Listed in order of preference. 0 terminalted. */
static const size_t test_channel_counts[] = {
	2,
	1,
	0
};

int cras_alsa_pcm_open(snd_pcm_t **handle, const char *dev,
		       snd_pcm_stream_t stream)
{
	return snd_pcm_open(handle,
			    dev,
			    stream,
			    SND_PCM_NONBLOCK |
			    SND_PCM_NO_AUTO_RESAMPLE |
			    SND_PCM_NO_AUTO_CHANNELS |
			    SND_PCM_NO_AUTO_FORMAT);
}

int cras_alsa_pcm_close(snd_pcm_t *handle)
{
	return snd_pcm_close(handle);
}

int cras_alsa_pcm_start(snd_pcm_t *handle)
{
	return snd_pcm_start(handle);
}

int cras_alsa_pcm_drain(snd_pcm_t *handle)
{
	return snd_pcm_drain(handle);
}

int cras_alsa_fill_properties(const char *dev, snd_pcm_stream_t stream,
			      size_t **rates, size_t **channel_counts)
{
	int rc;
	snd_pcm_t *handle;
	size_t i, num_found;
	snd_pcm_hw_params_t *params;

	snd_pcm_hw_params_alloca(&params);

	rc = snd_pcm_open(&handle,
			  dev,
			  stream,
			  SND_PCM_NONBLOCK |
			  SND_PCM_NO_AUTO_RESAMPLE |
			  SND_PCM_NO_AUTO_CHANNELS |
			  SND_PCM_NO_AUTO_FORMAT);
	if (rc < 0) {
		syslog(LOG_ERR, "snd_pcm_open_failed: %s", snd_strerror(rc));
		return rc;
	}

	rc = snd_pcm_hw_params_any(handle, params);
	if (rc < 0) {
		snd_pcm_close(handle);
		syslog(LOG_ERR, "snd_pcm_hw_params_any: %s", snd_strerror(rc));
		return rc;
	}

	*rates = malloc(sizeof(test_sample_rates));
	if (*rates == NULL) {
		snd_pcm_close(handle);
		return -ENOMEM;
	}
	*channel_counts = malloc(sizeof(test_channel_counts));
	if (*channel_counts == NULL) {
		free(*rates);
		snd_pcm_close(handle);
		return -ENOMEM;
	}

	num_found = 0;
	for (i = 0; test_sample_rates[i] != 0; i++) {
		rc = snd_pcm_hw_params_test_rate(handle, params,
						 test_sample_rates[i], 0);
		if (rc == 0)
			(*rates)[num_found++] = test_sample_rates[i];
	}
	(*rates)[num_found] = 0;

	num_found = 0;
	for (i = 0; test_channel_counts[i] != 0; i++) {
		rc = snd_pcm_hw_params_set_channels(handle, params,
						    test_channel_counts[i]);
		if (rc == 0)
			(*channel_counts)[num_found++] = test_channel_counts[i];
	}
	(*channel_counts)[num_found] = 0;

	snd_pcm_close(handle);

	return 0;
}

int cras_alsa_set_hwparams(snd_pcm_t *handle, struct cras_audio_format *format,
			   snd_pcm_uframes_t *buffer_frames)
{
	unsigned int rate, ret_rate;
	int err, dir;
	snd_pcm_uframes_t period_size;
	snd_pcm_hw_params_t *hwparams;

	rate = format->frame_rate;
	snd_pcm_hw_params_alloca(&hwparams);

	err = snd_pcm_hw_params_any(handle, hwparams);
	if (err < 0) {
		syslog(LOG_ERR, "hw_params_any failed %s\n", snd_strerror(err));
		return err;
	}
	/* Disable hardware resampling. */
	err = snd_pcm_hw_params_set_rate_resample(handle, hwparams, 0);
	if (err < 0) {
		syslog(LOG_ERR, "Disabling resampling %s\n", snd_strerror(err));
		return err;
	}
	/* Always interleaved. */
	err = snd_pcm_hw_params_set_access(handle, hwparams,
					   SND_PCM_ACCESS_MMAP_INTERLEAVED);
	if (err < 0) {
		syslog(LOG_ERR, "Setting interleaved %s\n", snd_strerror(err));
		return err;
	}
	/* Try to disable ALSA wakeups, we'll keep a timer. */
	if (snd_pcm_hw_params_can_disable_period_wakeup(hwparams)) {
		err = snd_pcm_hw_params_set_period_wakeup(handle, hwparams, 0);
		if (err < 0)
			syslog(LOG_WARNING, "disabling wakeups %s\n",
			       snd_strerror(err));
	}
	/* Set the sample format. */
	err = snd_pcm_hw_params_set_format(handle, hwparams,
					   format->format);
	if (err < 0) {
		syslog(LOG_ERR, "set format %s\n", snd_strerror(err));
		return err;
	}
	/* Set the stream rate. */
	ret_rate = rate;
	err = snd_pcm_hw_params_set_rate_near(handle, hwparams, &ret_rate, 0);
	if (err < 0) {
		syslog(LOG_ERR, "set_rate_near %iHz %s\n", rate,
		       snd_strerror(err));
		return err;
	}
	if (ret_rate != rate) {
		syslog(LOG_ERR, "tried for %iHz, settled for %iHz)\n", rate,
		       ret_rate);
		return -EINVAL;
	}
	/* Set the count of channels. */
	err = snd_pcm_hw_params_set_channels(handle, hwparams,
					     format->num_channels);
	if (err < 0) {
		syslog(LOG_ERR, "set_channels %s\n", snd_strerror(err));
		return err;
	}

	err = snd_pcm_hw_params_get_buffer_size_max(hwparams,
						    buffer_frames);
	if (err < 0)
		syslog(LOG_WARNING, "get buffer max %s\n", snd_strerror(err));

	err = snd_pcm_hw_params_set_buffer_size_near(handle, hwparams,
						     buffer_frames);
	if (err < 0) {
		syslog(LOG_ERR, "set_buffer_size_near %s\n", snd_strerror(err));
		return err;
	}
	dir = 0;
	period_size = *buffer_frames;
	err = snd_pcm_hw_params_set_period_size_near(handle, hwparams,
						     &period_size, &dir);
	if (err < 0) {
		syslog(LOG_ERR, "set_period_size_near %s\n", snd_strerror(err));
		return err;
	}
	syslog(LOG_DEBUG, "period, buffer size set to %u, %u\n",
	       (unsigned int)period_size, (unsigned int)*buffer_frames);
	/* Finally, write the parameters to the device. */
	err = snd_pcm_hw_params(handle, hwparams);
	if (err < 0) {
		syslog(LOG_ERR, "hw_params: %s\n", snd_strerror(err));
		return err;
	}

	return 0;
}

int cras_alsa_set_swparams(snd_pcm_t *handle)
{
	int err;
	snd_pcm_sw_params_t *swparams;
	snd_pcm_uframes_t boundary;

	snd_pcm_sw_params_alloca(&swparams);

	err = snd_pcm_sw_params_current(handle, swparams);
	if (err < 0) {
		syslog(LOG_ERR, "sw_params_current: %s\n", snd_strerror(err));
		return err;
	}
	err = snd_pcm_sw_params_get_boundary(swparams, &boundary);
	if (err < 0) {
		syslog(LOG_ERR, "get_boundary: %s\n", snd_strerror(err));
		return err;
	}
	err = snd_pcm_sw_params_set_stop_threshold(handle, swparams, boundary);
	if (err < 0) {
		syslog(LOG_ERR, "set_stop_threshold: %s\n", snd_strerror(err));
		return err;
	}
	/* Don't auto start. */
	err = snd_pcm_sw_params_set_start_threshold(handle, swparams, LONG_MAX);
	if (err < 0) {
		syslog(LOG_ERR, "set_stop_threshold: %s\n", snd_strerror(err));
		return err;
	}

	/* Disable period events. */
	err = snd_pcm_sw_params_set_period_event(handle, swparams, 0);
	if (err < 0) {
		syslog(LOG_ERR, "set_period_event: %s\n", snd_strerror(err));
		return err;
	}

	err = snd_pcm_sw_params(handle, swparams);
	if (err < 0) {
		syslog(LOG_ERR, "sw_params: %s\n", snd_strerror(err));
		return err;
	}
	return 0;
}

int cras_alsa_get_avail_frames(snd_pcm_t *handle, snd_pcm_uframes_t buf_size,
			       snd_pcm_uframes_t *used)
{
	snd_pcm_sframes_t frames;
	int rc = 0;

	frames = snd_pcm_avail(handle);
	if (frames == -EPIPE || frames == -ESTRPIPE) {
		cras_alsa_attempt_resume(handle);
		frames = 0;
	} else if (frames < 0) {
		syslog(LOG_INFO, "pcm_avail error %s\n", snd_strerror(frames));
		rc = frames;
		frames = 0;
	} else if (frames > buf_size)
		frames = buf_size;
	*used = frames;
	return rc;
}

int cras_alsa_get_delay_frames(snd_pcm_t *handle, snd_pcm_uframes_t buf_size,
			       snd_pcm_sframes_t *delay)
{
	int rc;

	rc = snd_pcm_delay(handle, delay);
	if (rc < 0)
		return rc;
	if (*delay > buf_size)
		*delay = buf_size;
	if (*delay < 0)
		*delay = 0;
	return 0;
}

int cras_alsa_attempt_resume(snd_pcm_t *handle)
{
	int rc;

	syslog(LOG_INFO, "System suspended.");
	while ((rc = snd_pcm_resume(handle)) == -EAGAIN)
		usleep(ALSA_SUSPENDED_SLEEP_TIME_US);
	if (rc < 0) {
		syslog(LOG_INFO, "System suspended, failed to resume %s.",
		       snd_strerror(rc));
		rc = snd_pcm_prepare(handle);
		if (rc < 0)
			syslog(LOG_INFO, "Suspended, failed to prepare: %s.",
			       snd_strerror(rc));
	}
	return rc;
}

int cras_alsa_mmap_begin(snd_pcm_t *handle, unsigned int format_bytes,
			 uint8_t **dst, snd_pcm_uframes_t *offset,
			 snd_pcm_uframes_t *frames, unsigned int *underruns)
{
	int rc;
	unsigned int attempts = 0;
	const snd_pcm_channel_area_t *my_areas;

	while (attempts++ < MAX_MMAP_BEGIN_ATTEMPTS) {
		rc = snd_pcm_mmap_begin(handle, &my_areas, offset, frames);
		if (rc == -ESTRPIPE) {
			/* First handle suspend/resume. */
			rc = cras_alsa_attempt_resume(handle);
			if (rc < 0)
				return rc;
		} else if (rc < 0) {
			*underruns = *underruns + 1;
			/* If we can recover, continue and try again. */
			if (snd_pcm_recover(handle, rc, 0) == 0)
				continue;
			syslog(LOG_INFO, "recover failed begin: %s\n",
			       snd_strerror(rc));
			return rc;
		}
		if (*frames == 0) {
			syslog(LOG_INFO, "mmap_begin set frames to 0.");
			return -EIO;
		}
		*dst = (uint8_t *)my_areas[0].addr + (*offset) * format_bytes;
		return 0;
	}
	return -EIO;
}

int cras_alsa_mmap_commit(snd_pcm_t *handle, snd_pcm_uframes_t offset,
			  snd_pcm_uframes_t frames, unsigned int *underruns)
{
	int rc;
	snd_pcm_sframes_t res;

	res = snd_pcm_mmap_commit(handle, offset, frames);
	if (res != frames) {
		res = res >= 0 ? (int)-EPIPE : res;
		if (res == -ESTRPIPE) {
			/* First handle suspend/resume. */
			rc = cras_alsa_attempt_resume(handle);
			if (rc < 0)
				return rc;
		} else {
			*underruns = *underruns + 1;
			/* If we can recover, continue and try again. */
			rc = snd_pcm_recover(handle, res, 0);
			if (rc < 0) {
				syslog(LOG_ERR,
				       "mmap_commit: pcm_recover failed: %s\n",
				       snd_strerror(rc));
				return rc;
			}
		}
	}
	return 0;
}
