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
#include "cras_iodev_list.h"
#include "cras_rstream.h"
#include "cras_system_state.h"
#include "cras_util.h"
#include "audio_thread.h"
#include "utlist.h"

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

int cras_iodev_set_format(struct cras_iodev *iodev,
			  struct cras_audio_format *fmt)
{
	size_t actual_rate, actual_num_channels;
	int rc;

	/* If this device isn't already using a format, try to match the one
	 * requested in "fmt". */
	if (iodev->format == NULL) {
		iodev->format = malloc(sizeof(struct cras_audio_format));
		if (!iodev->format)
			return -ENOMEM;
		*iodev->format = *fmt;

		if (iodev->update_supported_formats) {
			rc = iodev->update_supported_formats(iodev);
			if (rc) {
				syslog(LOG_ERR, "Failed to update formats");
				goto error;
			}
		}


		actual_rate = get_best_rate(iodev, fmt->frame_rate);
		actual_num_channels = get_best_channel_count(iodev,
							     fmt->num_channels);
		if (actual_rate == 0 || actual_num_channels == 0) {
			/* No compatible frame rate found. */
			rc = -EINVAL;
			goto error;
		}
		iodev->format->frame_rate = actual_rate;
		iodev->format->num_channels = actual_num_channels;
		/* TODO(dgreid) - allow other formats. */
		iodev->format->format = SND_PCM_FORMAT_S16_LE;

		if (iodev->update_channel_layout) {
			rc = iodev->update_channel_layout(iodev);
			if (rc < 0) {
				/* Fall back to stereo when no matching layout
				 * is found. */
				actual_num_channels = get_best_channel_count(
						iodev, 2);
				if (actual_num_channels == 0)
					goto error;
				iodev->format->num_channels =
						actual_num_channels;
			}
		}
		cras_iodev_alloc_dsp(iodev);
	}

	*fmt = *(iodev->format);
	return 0;

error:
	free(iodev->format);
	iodev->format = NULL;
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
	if (iodev->format) {
		free(iodev->format);
		iodev->format = NULL;
	}
}

static void cras_iodev_alloc_dsp(struct cras_iodev *iodev)
{
	const char *purpose;

	if (iodev->direction == CRAS_STREAM_OUTPUT)
		purpose = "playback";
	else
		purpose = "capture";

	cras_iodev_free_dsp(iodev);
	iodev->dsp_context = cras_dsp_context_new(iodev->format->num_channels,
						  iodev->format->frame_rate,
						  purpose);
	cras_iodev_update_dsp(iodev);
}

void cras_iodev_free_dsp(struct cras_iodev *iodev)
{
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
				       struct cras_timespec *ts)
{
	cras_clock_gettime(CLOCK_MONOTONIC, ts);

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
				      struct cras_timespec *ts)
{
	long tmp;

	cras_clock_gettime(CLOCK_MONOTONIC, ts);

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

/*
 * The rules are (in decision order):
 * - A non-null node is better than a null node.
 * - A plugged node is better than an unplugged node.
 * - A selected node is better than an unselected node.
 * - A node with high priority is better.
 * - A more recently plugged node is better.
 */
int cras_ionode_better(struct cras_ionode *a, struct cras_ionode *b)
{
	int select_a, select_b;

	if (a && !b)
		return 1;
	if (!a && b)
		return 0;

	if (a->plugged > b->plugged)
		return 1;
	if (a->plugged < b->plugged)
		return 0;

	select_a = cras_iodev_list_node_selected(a);
	select_b = cras_iodev_list_node_selected(b);

	if (select_a > select_b)
		return 1;
	if (select_a < select_b)
		return 0;

	if (a->priority > b->priority)
		return 1;
	if (a->priority < b->priority)
		return 0;

	if (timeval_after(&a->plugged_time, &b->plugged_time))
		return 1;
	if (timeval_after(&b->plugged_time, &a->plugged_time))
		return 0;

	return 0;
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

	volume = (unsigned int)min(value, 100);
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
	default:
		return -EINVAL;
	}

	return 0;
}

struct cras_ionode *cras_iodev_get_best_node(const struct cras_iodev *iodev)
{
	struct cras_ionode *output;
	struct cras_ionode *best;

	/* Take the first entry as a starting point. */
	best = iodev->nodes;

	DL_FOREACH(iodev->nodes, output)
		if (cras_ionode_better(output, best))
			best = output;
	return best;
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

void cras_iodev_set_software_volume(struct cras_iodev *iodev,
				    float volume_scaler)
{
	/* TODO(alhli): Also need to set volume scaler via IPC message. */
        iodev->software_volume_scaler = volume_scaler;
}
