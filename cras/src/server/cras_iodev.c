/* Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <pthread.h>
#include <stdlib.h>
#include <sys/param.h>
#include <sys/time.h>
#include <syslog.h>
#include <time.h>

#include "buffer_share.h"
#include "cras_audio_area.h"
#include "cras_dsp.h"
#include "cras_dsp_pipeline.h"
#include "cras_iodev.h"
#include "cras_iodev_list.h"
#include "cras_mix.h"
#include "cras_rstream.h"
#include "cras_system_state.h"
#include "cras_util.h"
#include "dev_stream.h"
#include "utlist.h"
#include "rate_estimator.h"
#include "softvol_curve.h"

static const struct timespec rate_estimation_window_sz = {
	1, 0 /* 1 sec. */
};
static const double rate_estimation_smooth_factor = 0.9f;

static void cras_iodev_alloc_dsp(struct cras_iodev *iodev);

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

/* Finds the best match for the channel count.  The following match rules
 * will apply in order and return the value once matched:
 * 1. Match the exact given channel count.
 * 2. Match the preferred channel count.
 * 3. The first channel count in the list.
 */
static size_t get_best_channel_count(struct cras_iodev *iodev, size_t count)
{
	static const size_t preferred_channel_count = 2;
	size_t i;

	assert(iodev->supported_channel_counts[0] != 0);

	for (i = 0; iodev->supported_channel_counts[i] != 0; i++) {
		if (iodev->supported_channel_counts[i] == count)
			return count;
	}

	/* If provided count is not supported, search for preferred
	 * channel count to which we're good at converting.
	 */
	for (i = 0; iodev->supported_channel_counts[i] != 0; i++) {
		if (iodev->supported_channel_counts[i] ==
				preferred_channel_count)
			return preferred_channel_count;
	}

	return iodev->supported_channel_counts[0];
}

/* finds the best match for the current format. If no exact match is
 * found, use the first. */
static snd_pcm_format_t get_best_pcm_format(struct cras_iodev *iodev,
					    snd_pcm_format_t fmt)
{
	size_t i;

	for (i = 0; iodev->supported_formats[i] != 0; i++) {
		if (fmt == iodev->supported_formats[i])
			return fmt;
	}

	return iodev->supported_formats[0];
}

/* Set default channel count and layout to an iodev. */
static void set_default_channel_count_layout(struct cras_iodev *iodev)
{
	static const int stereo_channel_count = 2;
	static const int8_t stereo_layout[] =
			{0, 1, -1, -1, -1, -1, -1, -1, -1, -1, -1};
	size_t num_channels;

	/* Fall back to stereo when no matching layout is found. If we
	 * see any device only supports channel count > 2, make sure
	 * it has a default channel layout. */
	num_channels = get_best_channel_count(iodev, stereo_channel_count);
	iodev->format->num_channels = num_channels;
	iodev->ext_format->num_channels = num_channels;
	if (num_channels == stereo_channel_count) {
		cras_audio_format_set_channel_layout(iodev->format,
						     stereo_layout);
		cras_audio_format_set_channel_layout(iodev->ext_format,
						     stereo_layout);
	}
}

/* Applies the DSP to the samples for the iodev if applicable. */
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

	cras_dsp_pipeline_apply(pipeline,
				buf,
				frames);

	cras_dsp_put_pipeline(ctx);
}

static void cras_iodev_free_dsp(struct cras_iodev *iodev)
{
	if (iodev->dsp_context) {
		cras_dsp_context_free(iodev->dsp_context);
		iodev->dsp_context = NULL;
	}
}

/* Modifies the format to the one that will be presented to the device after
 * any format changes from the DSP. */
static inline void adjust_dev_fmt_for_dsp(const struct cras_iodev *iodev)
{
	struct cras_dsp_context *ctx = iodev->dsp_context;

	if (!ctx || !cras_dsp_get_pipeline(ctx))
		return;

	if (iodev->direction == CRAS_STREAM_OUTPUT) {
		iodev->format->num_channels =
			cras_dsp_num_output_channels(ctx);
		iodev->ext_format->num_channels =
			cras_dsp_num_input_channels(ctx);
	} else {
		iodev->format->num_channels =
			cras_dsp_num_input_channels(ctx);
		iodev->ext_format->num_channels =
			cras_dsp_num_output_channels(ctx);
	}

	cras_dsp_put_pipeline(ctx);
}

int cras_iodev_set_format(struct cras_iodev *iodev,
			  struct cras_audio_format *fmt)
{
	size_t actual_rate, actual_num_channels;
	snd_pcm_format_t actual_format;
	int rc;

	/* If this device isn't already using a format, try to match the one
	 * requested in "fmt". */
	if (iodev->format == NULL) {
		iodev->format = malloc(sizeof(struct cras_audio_format));
		iodev->ext_format = malloc(sizeof(struct cras_audio_format));
		if (!iodev->format || !iodev->ext_format)
			return -ENOMEM;
		*iodev->format = *fmt;
		*iodev->ext_format = *fmt;

		if (iodev->update_supported_formats) {
			rc = iodev->update_supported_formats(iodev);
			if (rc) {
				syslog(LOG_ERR, "Failed to update formats");
				goto error;
			}
		}

		cras_iodev_alloc_dsp(iodev);
		if (iodev->dsp_context)
			adjust_dev_fmt_for_dsp(iodev);

		actual_rate = get_best_rate(iodev, fmt->frame_rate);
		actual_num_channels = get_best_channel_count(iodev,
					iodev->format->num_channels);
		actual_format = get_best_pcm_format(iodev, fmt->format);
		if (actual_rate == 0 || actual_num_channels == 0 ||
		    actual_format == 0) {
			/* No compatible frame rate found. */
			rc = -EINVAL;
			goto error;
		}
		iodev->format->frame_rate = actual_rate;
		iodev->ext_format->frame_rate = actual_rate;
		iodev->format->format = actual_format;
		iodev->ext_format->format = actual_format;
		if (iodev->format->num_channels != actual_num_channels) {
			/* If the DSP for this device doesn't match, drop it. */
			iodev->format->num_channels = actual_num_channels;
			iodev->ext_format->num_channels = actual_num_channels;
			cras_iodev_free_dsp(iodev);
		}

		if (iodev->update_channel_layout) {
			rc = iodev->update_channel_layout(iodev);
			if (rc < 0) {
				set_default_channel_count_layout(iodev);
				cras_iodev_free_dsp(iodev);
			}
		}

		if (!iodev->rate_est)
			iodev->rate_est = rate_estimator_create(
						actual_rate,
						&rate_estimation_window_sz,
						rate_estimation_smooth_factor);
		else
			rate_estimator_reset_rate(iodev->rate_est, actual_rate);
	}

	/* Fill the format information back to stream. For capture stream,
	 * leave the channel count/layout as it is.
	 */
	fmt->format = iodev->ext_format->format;
	fmt->frame_rate = iodev->ext_format->frame_rate;
	if (iodev->direction == CRAS_STREAM_OUTPUT) {
		fmt->num_channels = iodev->ext_format->num_channels;
		cras_audio_format_set_channel_layout(fmt,
				iodev->ext_format->channel_layout);
	}

	return 0;

error:
	free(iodev->format);
	free(iodev->ext_format);
	iodev->format = NULL;
	iodev->ext_format = NULL;
	return rc;
}

void cras_iodev_update_dsp(struct cras_iodev *iodev)
{
	if (!iodev->dsp_context)
		return;

	cras_dsp_set_variable(iodev->dsp_context, "dsp_name",
			      iodev->dsp_name ? : "");
	cras_dsp_load_pipeline(iodev->dsp_context);
}

void cras_iodev_free_format(struct cras_iodev *iodev)
{
	free(iodev->format);
	free(iodev->ext_format);
	iodev->format = NULL;
	iodev->ext_format = NULL;
}


void cras_iodev_init_audio_area(struct cras_iodev *iodev,
				int num_channels)
{
	if (iodev->area)
		cras_iodev_free_audio_area(iodev);

	iodev->area = cras_audio_area_create(num_channels);
	cras_audio_area_config_channels(iodev->area, iodev->format);
}

void cras_iodev_free_audio_area(struct cras_iodev *iodev)
{
	if (!iodev->area)
		return;

	cras_audio_area_destroy(iodev->area);
	iodev->area = NULL;
}

void cras_iodev_free_resources(struct cras_iodev *iodev)
{
	cras_iodev_free_dsp(iodev);
	rate_estimator_destroy(iodev->rate_est);
}

static void cras_iodev_alloc_dsp(struct cras_iodev *iodev)
{
	const char *purpose;

	if (iodev->direction == CRAS_STREAM_OUTPUT)
		purpose = "playback";
	else
		purpose = "capture";

	cras_iodev_free_dsp(iodev);
	iodev->dsp_context = cras_dsp_context_new(iodev->ext_format->frame_rate,
						  purpose);
	cras_iodev_update_dsp(iodev);
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

/* This is called when a node is plugged/unplugged */
static void plug_node(struct cras_ionode *node, int plugged)
{
	if (node->plugged == plugged)
		return;
	node->plugged = plugged;
	if (plugged) {
		gettimeofday(&node->plugged_time, NULL);
	}
	cras_iodev_list_notify_nodes_changed();
}

static void set_node_volume(struct cras_ionode *node, int value)
{
	struct cras_iodev *dev = node->dev;
	unsigned int volume;

	if (dev->direction != CRAS_STREAM_OUTPUT)
		return;

	volume = (unsigned int)MIN(value, 100);
	node->volume = volume;
	if (dev->set_volume)
		dev->set_volume(dev);

	cras_iodev_list_notify_node_volume(node);
}

static void set_node_capture_gain(struct cras_ionode *node, int value)
{
	struct cras_iodev *dev = node->dev;

	if (dev->direction != CRAS_STREAM_INPUT)
		return;

	node->capture_gain = (long)value;
	if (dev->set_capture_gain)
		dev->set_capture_gain(dev);

	cras_iodev_list_notify_node_capture_gain(node);
}

static void set_node_left_right_swapped(struct cras_ionode *node, int value)
{
	struct cras_iodev *dev = node->dev;
	int rc;

	if (!dev->set_swap_mode_for_node)
		return;
	rc = dev->set_swap_mode_for_node(dev, node, value);
	if (rc) {
		syslog(LOG_ERR,
		       "Failed to set swap mode on node %s to %d; error %d",
		       node->name, value, rc);
		return;
	}
	node->left_right_swapped = value;
	cras_iodev_list_notify_node_left_right_swapped(node);
	return;
}

int cras_iodev_set_node_attr(struct cras_ionode *ionode,
			     enum ionode_attr attr, int value)
{
	switch (attr) {
	case IONODE_ATTR_PLUGGED:
		plug_node(ionode, value);
		break;
	case IONODE_ATTR_VOLUME:
		set_node_volume(ionode, value);
		break;
	case IONODE_ATTR_CAPTURE_GAIN:
		set_node_capture_gain(ionode, value);
		break;
	case IONODE_ATTR_SWAP_LEFT_RIGHT:
		set_node_left_right_swapped(ionode, value);
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

void cras_iodev_add_node(struct cras_iodev *iodev, struct cras_ionode *node)
{
	DL_APPEND(iodev->nodes, node);
	cras_iodev_list_notify_nodes_changed();
}

void cras_iodev_rm_node(struct cras_iodev *iodev, struct cras_ionode *node)
{
	DL_DELETE(iodev->nodes, node);
	cras_iodev_list_notify_nodes_changed();
}

void cras_iodev_set_active_node(struct cras_iodev *iodev,
				struct cras_ionode *node)
{
	iodev->active_node = node;
	cras_iodev_list_notify_active_node_changed();
}

float cras_iodev_get_software_volume_scaler(struct cras_iodev *iodev)
{
	unsigned int volume;

	volume = cras_iodev_adjust_active_node_volume(
			iodev, cras_system_get_volume());

	if (iodev->active_node && iodev->active_node->softvol_scalers)
		return iodev->active_node->softvol_scalers[volume];
	return softvol_get_scaler(volume);
}

int cras_iodev_add_stream(struct cras_iodev *iodev,
			  struct dev_stream *stream)
{
	struct cras_rstream *rstream = stream->stream;

	DL_APPEND(iodev->streams, stream);

	buffer_share_add_id(iodev->buf_state, stream->stream->stream_id, NULL);

	iodev->min_cb_level = MIN(iodev->min_cb_level, rstream->cb_threshold);
	iodev->max_cb_level = MAX(iodev->max_cb_level, rstream->cb_threshold);
	return 0;
}

int cras_iodev_rm_stream(struct cras_iodev *iodev,
			 const struct dev_stream *stream)
{
	buffer_share_rm_id(iodev->buf_state, stream->stream->stream_id);
	DL_DELETE(iodev->streams, stream);
	return 0;
}

unsigned int cras_iodev_stream_offset(struct cras_iodev *iodev,
				      struct dev_stream *stream)
{
	return buffer_share_id_offset(iodev->buf_state,
				      stream->stream->stream_id);
}

void cras_iodev_stream_written(struct cras_iodev *iodev,
			       struct dev_stream *stream,
			       unsigned int nwritten)
{
	buffer_share_offset_update(iodev->buf_state,
				   stream->stream->stream_id, nwritten);
}

unsigned int cras_iodev_all_streams_written(struct cras_iodev *iodev)
{
	return buffer_share_get_new_write_point(iodev->buf_state);
}

unsigned int cras_iodev_max_stream_offset(const struct cras_iodev *iodev)
{
	unsigned int max = 0;
	struct dev_stream *curr;

	DL_FOREACH(iodev->streams, curr) {
		max = MAX(max,
			  buffer_share_id_offset(iodev->buf_state,
						 curr->stream->stream_id));
	}

	return max;
}

int cras_iodev_open(struct cras_iodev *iodev)
{
	int rc;

	rc = iodev->open_dev(iodev);
	if (rc < 0)
		return rc;

	iodev->buf_state = buffer_share_create(iodev->buffer_size);
	return 0;
}

int cras_iodev_close(struct cras_iodev *iodev)
{
	if (!iodev->is_open(iodev))
		return 0;
	buffer_share_destroy(iodev->buf_state);
	iodev->buf_state = NULL;
	return iodev->close_dev(iodev);
}

int cras_iodev_put_input_buffer(struct cras_iodev *iodev, unsigned int nframes)
{
	rate_estimator_add_frames(iodev->rate_est, -nframes);
	return iodev->put_buffer(iodev, nframes);
}

int cras_iodev_put_output_buffer(struct cras_iodev *iodev, uint8_t *frames,
				 unsigned int nframes)
{
	const struct cras_audio_format *fmt = iodev->format;

	if (cras_system_get_mute()) {
		const unsigned int frame_bytes = cras_get_format_bytes(fmt);
		cras_mix_mute_buffer(frames, frame_bytes, nframes);
	} else {
		apply_dsp(iodev, frames, nframes);

		if (cras_iodev_software_volume_needed(iodev)) {
			unsigned int nsamples = nframes * fmt->num_channels;
			float scaler =
				cras_iodev_get_software_volume_scaler(iodev);

			cras_scale_buffer(SND_PCM_FORMAT_S16_LE, frames,
					  nsamples, scaler);
		}
	}

	rate_estimator_add_frames(iodev->rate_est, nframes);
	return iodev->put_buffer(iodev, nframes);
}

int cras_iodev_get_input_buffer(struct cras_iodev *iodev,
				struct cras_audio_area **area,
				unsigned *frames)
{
	const struct cras_audio_format *fmt = iodev->format;
	const unsigned int frame_bytes = cras_get_format_bytes(fmt);
	uint8_t *hw_buffer;
	int rc;

	rc = iodev->get_buffer(iodev, area, frames);
	if (rc < 0 || *frames == 0)
		return rc;

	/* TODO(dgreid) - This assumes interleaved audio. */
	hw_buffer = (*area)->channels[0].buf;

	if (cras_system_get_capture_mute())
		cras_mix_mute_buffer(hw_buffer, frame_bytes, *frames);
	else
		apply_dsp(iodev, hw_buffer, *frames); /* TODO-applied 2x */

	return rc;
}

int cras_iodev_get_output_buffer(struct cras_iodev *iodev,
				 struct cras_audio_area **area,
				 unsigned *frames)
{
	return iodev->get_buffer(iodev, area, frames);
}

int cras_iodev_update_rate(struct cras_iodev *iodev, unsigned int level)
{
	struct timespec now;

	clock_gettime(CLOCK_MONOTONIC, &now);
	return rate_estimator_check(iodev->rate_est, level, &now);
}

int cras_iodev_reset_rate_estimator(const struct cras_iodev *iodev)
{
	rate_estimator_reset_rate(iodev->rate_est,
				  iodev->ext_format->frame_rate);
	return 0;
}

double cras_iodev_get_est_rate_ratio(const struct cras_iodev *iodev)
{
	return rate_estimator_get_rate(iodev->rate_est) /
			iodev->ext_format->frame_rate;
}

int cras_iodev_get_dsp_delay(const struct cras_iodev *iodev)
{
	struct cras_dsp_context *ctx;
	struct pipeline *pipeline;
	int delay;

	ctx = iodev->dsp_context;
	if (!ctx)
		return 0;

	pipeline = cras_dsp_get_pipeline(ctx);
	if (!pipeline)
		return 0;

	delay = cras_dsp_pipeline_get_delay(pipeline);

	cras_dsp_put_pipeline(ctx);
	return delay;
}
