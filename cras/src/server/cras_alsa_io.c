/* Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <alsa/asoundlib.h>
#include <alsa/use-case.h>
#include <errno.h>
#include <limits.h>
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
#include "cras_iodev.h"
#include "cras_iodev_list.h"
#include "cras_messages.h"
#include "cras_rclient.h"
#include "cras_shm.h"
#include "cras_system_state.h"
#include "cras_types.h"
#include "cras_util.h"
#include "cras_volume_curve.h"
#include "utlist.h"

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
};

static void init_device_settings(struct alsa_io *aio);

/*
 * iodev callbacks.
 */

static int frames_queued(const struct cras_iodev *iodev)
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

static int delay_frames(const struct cras_iodev *iodev)
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

static int open_dev(struct cras_iodev *iodev)
{
	struct alsa_io *aio = (struct alsa_io *)iodev;
	snd_pcm_t *handle;
	int rc;

	/* This is called after the first stream added so configure for it.
	 * format must be set before opening the device.
	 */
	if (iodev->format == NULL)
		return -EINVAL;
	/* TODO(dgreid) - allow more formats here. */
	iodev->format->format = SND_PCM_FORMAT_S16_LE;
	aio->num_underruns = 0;

	syslog(LOG_DEBUG, "Configure alsa device %s rate %zuHz, %zu channels",
	       aio->dev, iodev->format->frame_rate,
	       iodev->format->num_channels);
	handle = 0; /* Avoid unused warning. */
	rc = cras_alsa_pcm_open(&handle, aio->dev, aio->alsa_stream);
	if (rc < 0)
		return rc;

	init_device_settings(aio);

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

static int is_open(const struct cras_iodev *iodev)
{
	struct alsa_io *aio = (struct alsa_io *)iodev;

	return !!aio->handle;
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

/*
 * Alsa helper functions.
 */

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
	if (!is_open(&aio->base))
		return;

	curve = get_curve_for_active_output(aio);
	cras_system_set_volume_limits(
			curve->get_dBFS(curve, 1), /* min */
			curve->get_dBFS(curve, CRAS_MAX_SYSTEM_VOLUME));
}

/* Sets the alsa mute state for this iodev. */
static void set_alsa_mute(const struct alsa_io *aio, int muted)
{
	if (!is_open(&aio->base))
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
	if (!is_open(&aio->base))
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
	if (!is_open(&aio->base))
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

	if (direction != CRAS_STREAM_INPUT && direction != CRAS_STREAM_OUTPUT)
		return NULL;

	aio = (struct alsa_io *)calloc(1, sizeof(*aio));
	if (!aio)
		return NULL;
	iodev = &aio->base;
	iodev->direction = direction;

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

	if (direction == CRAS_STREAM_INPUT) {
		aio->alsa_stream = SND_PCM_STREAM_CAPTURE;
		aio->base.set_capture_gain = set_alsa_capture_gain;
		aio->base.set_capture_mute = set_alsa_capture_gain;
	} else {
		aio->alsa_stream = SND_PCM_STREAM_PLAYBACK;
		aio->base.set_volume = set_alsa_volume;
		aio->base.set_mute = set_alsa_volume;
	}
	iodev->open_dev = open_dev;
	iodev->close_dev = close_dev;
	iodev->is_open = is_open;
	iodev->update_supported_formats = update_supported_formats;
	iodev->set_as_default = set_as_default;
	iodev->frames_queued = frames_queued;
	iodev->delay_frames = delay_frames;
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
