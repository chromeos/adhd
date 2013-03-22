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
#define INTERNAL_SPEAKER "Speaker"
#define INTERNAL_MICROPHONE "Internal Mic"

/* For USB, pad the output buffer.  This avoids a situation where there isn't a
 * complete URB's worth of audio ready to be transmitted when it is requested.
 * The URB interval does track directly to the audio clock, making it hard to
 * predict the exact interval. */
#define USB_EXTRA_BUFFER_FRAMES 768


/* This extends cras_ionode to include alsa-specific information.
 * Members:
 *    mixer_output - From cras_alsa_mixer.
 *    jack_curve - In absense of a mixer output, holds a volume curve to use
 *        when this jack is plugged.
 *    jack - The jack associated with the jack_curve (if it exists).
 */
struct alsa_output_node {
	struct cras_ionode base;
	struct cras_alsa_mixer_output *mixer_output;
	struct cras_volume_curve *jack_curve;
	const struct cras_alsa_jack *jack;
};

struct alsa_input_node {
	struct cras_ionode base;
	struct mixer_volume_control* mixer_input;
	const struct cras_alsa_jack *jack;
};

/* Child of cras_iodev, alsa_io handles ALSA interaction for sound devices.
 * base - The cras_iodev structure "base class".
 * dev - String that names this device (e.g. "hw:0,0").
 * device_index - ALSA index of device, Y in "hw:X:Y".
 * next_ionode_index - The index we will give to the next ionode. Each ionode
 *     have a unique index within the iodev.
 * card_type - the type of the card this iodev belongs.
 * is_first - true if this is the first iodev on the card.
 * handle - Handle to the opened ALSA device.
 * num_underruns - Number of times we have run out of data (playback only).
 * alsa_stream - Playback or capture type.
 * mixer - Alsa mixer used to control volume and mute of the device.
 * jack_list - List of alsa jack controls for this device.
 * ucm - ALSA use case manager, if configuration is found.
 * mmap_offset - offset returned from mmap_begin.
 * dsp_name_default - the default dsp name for the device. It can be overridden
 *     by the jack specific dsp name.
 */
struct alsa_io {
	struct cras_iodev base;
	char *dev;
	uint32_t device_index;
	uint32_t next_ionode_index;
	enum CRAS_ALSA_CARD_TYPE card_type;
	int is_first;
	snd_pcm_t *handle;
	unsigned int num_underruns;
	snd_pcm_stream_t alsa_stream;
	struct cras_alsa_mixer *mixer;
	struct cras_alsa_jack_list *jack_list;
	snd_use_case_mgr_t *ucm;
	snd_pcm_uframes_t mmap_offset;
	const char *dsp_name_default;
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

	/* Assign pcm handle then initialize device settings. */
	aio->handle = handle;
	init_device_settings(aio);

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

static void update_active_node(struct cras_iodev *iodev)
{
	struct cras_ionode *best_node;

	best_node = cras_iodev_get_best_node(iodev);
	alsa_iodev_set_active_node(iodev, best_node);
	cras_iodev_move_stream_type_top_prio(CRAS_STREAM_TYPE_DEFAULT,
					     iodev->direction);
}

/*
 * Alsa helper functions.
 */

static struct alsa_output_node *get_active_output(const struct alsa_io *aio)
{
	return (struct alsa_output_node *)aio->base.active_node;
}

static struct alsa_input_node *get_active_input(const struct alsa_io *aio)
{
	return (struct alsa_input_node *)aio->base.active_node;
}

/* Gets the curve for the active output. */
static const struct cras_volume_curve *get_curve_for_active_output(
		const struct alsa_io *aio)
{
	struct alsa_output_node *aout = get_active_output(aio);

	if (aout && aout->mixer_output && aout->mixer_output->volume_curve)
		return aout->mixer_output->volume_curve;
	else if (aout && aout->jack_curve)
		return aout->jack_curve;
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
	struct alsa_output_node *aout;

	if (!is_open(&aio->base))
		return;

	aout = get_active_output(aio);
	cras_alsa_mixer_set_mute(
		aio->mixer,
		muted,
		aout ? aout->mixer_output : NULL);
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
	struct alsa_output_node *aout;

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
	aout = get_active_output(aio);
	cras_alsa_mixer_set_dBFS(
		aio->mixer,
		curve->get_dBFS(curve, volume),
		aout ? aout->mixer_output : NULL);
	/* Mute for zero. */
	set_alsa_mute(aio, mute || (volume == 0));
}

/* Sets the capture gain to the current system input gain level, given in dBFS.
 * Set mute based on the system mute state.  This gain can be positive or
 * negative and might be adjusted often if and app is running an AGC. */
static void set_alsa_capture_gain(struct cras_iodev *iodev)
{
	const struct alsa_io *aio = (const struct alsa_io *)iodev;
	struct alsa_input_node *ain;

	assert(aio);
	if (aio->mixer == NULL)
		return;

	/* Only set the volume if the dev is active. */
	if (!is_open(&aio->base))
		return;

	ain = get_active_input(aio);
	cras_alsa_mixer_set_capture_dBFS(
			aio->mixer,
			cras_system_get_capture_gain(),
			ain ? ain->mixer_input : NULL);
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
		struct alsa_input_node *ain = get_active_input(aio);
		if (ain)
			mixer_input = ain->mixer_input;
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
	struct cras_ionode *node, *tmp;
	struct alsa_output_node *aout;

	free(aio->base.supported_rates);
	free(aio->base.supported_channel_counts);

	DL_FOREACH_SAFE(aio->base.nodes, node, tmp) {
		if (aio->base.direction == CRAS_STREAM_OUTPUT) {
			aout = (struct alsa_output_node *)node;
			cras_volume_curve_destroy(aout->jack_curve);
		}
		cras_iodev_rm_node(&aio->base, node);
		free(node);
	}

	free((void *)aio->dsp_name_default);
	free(aio->dev);
}

/* Returns true if this is the first internal device */
static int first_internal_device(struct alsa_io *aio)
{
	return aio->is_first && aio->card_type == ALSA_CARD_TYPE_INTERNAL;
}

/* Returns true if there is already a node created with the given name */
static int has_node(struct alsa_io *aio, const char *name)
{
	struct cras_ionode *node;

	DL_FOREACH(aio->base.nodes, node)
		if (!strcmp(node->name, name))
			return 1;

	return 0;
}

/* Returns true if string s ends with the given suffix */
int endswith(const char *s, const char *suffix)
{
	size_t n = strlen(s);
	size_t m = strlen(suffix);
	return n >= m && !strcmp(s + (n - m), suffix);
}

/* Sets the initial plugged state and priority of a node based on its
 * name. From the highest priority to the lowest priority:
 *
 * Priority 3 -- USB headset, external headphone, external microphone. This has
 * the highest priority because it must be plugged intentionally by the user.
 *
 * Priority 2 -- HDMI/Display Port. This has lower priority because the user may
 * just want to output the video through the HDMI/DP connection.
 *
 * Priority 1 -- Internal speaker and microphone. This is used whenever there
 * are no external nodes plugged.
 *
 * Priority 0 -- This is added only when there are no other nodes found for a
 * device.
 */
static void set_node_initial_state(struct cras_ionode *node,
				   enum CRAS_ALSA_CARD_TYPE card_type)
{
	static const struct {
		const char *name;
		int priority;
		int initial_plugged;
		enum CRAS_NODE_TYPE type;
	} prios[] = {
		{ "(default)", 0, 1, CRAS_NODE_TYPE_UNKNOWN},
		{ INTERNAL_SPEAKER, 1, 1, CRAS_NODE_TYPE_INTERNAL_SPEAKER },
		{ INTERNAL_MICROPHONE, 1, 1, CRAS_NODE_TYPE_INTERNAL_MIC },
		{ "HDMI", 2, 0, CRAS_NODE_TYPE_HDMI },
		{ "IEC958", 2, 0, CRAS_NODE_TYPE_HDMI },
		{ "Headphone", 3, 0, CRAS_NODE_TYPE_HEADPHONE },
		{ "Front Headphone", 3, 0, CRAS_NODE_TYPE_HEADPHONE },
		{ "Mic", 3, 0, CRAS_NODE_TYPE_MIC },
	};
	unsigned i;

	node->type = CRAS_NODE_TYPE_UNKNOWN;
	/* Go through the known names */
	for (i = 0; i < ARRAY_SIZE(prios); i++)
		if (!strncmp(node->name, prios[i].name,
			     strlen(prios[i].name))) {
			node->priority = prios[i].priority;
			node->plugged = prios[i].initial_plugged;
			node->type = prios[i].type;
			if (node->plugged)
				gettimeofday(&node->plugged_time, NULL);
			break;
		}

	/* If we didn't find a matching name above, but the node is a jack node,
	 * give it a high priority. This matches node names like "DAISY-I2S Mic
	 * Jack" */
	if (i == ARRAY_SIZE(prios)) {
		if (endswith(node->name, "Jack")) {
			node->priority = 3;
			if (node->dev->direction == CRAS_STREAM_OUTPUT)
				node->type = CRAS_NODE_TYPE_HEADPHONE;
			else
				node->type = CRAS_NODE_TYPE_MIC;
		}
	}

	/* Regardless of the node name of a USB headset (it can be "Speaker"),
	 * we want to give it a high priority, the same as external 3.5mm
	 * Headphone/Mic. */
	if (card_type == ALSA_CARD_TYPE_USB) {
		node->priority = 3;
		node->type = CRAS_NODE_TYPE_USB;
	}
}

static const char *get_output_node_name(struct alsa_io *aio,
	struct cras_alsa_mixer_output *cras_output)
{
	if (cras_output)
		return cras_alsa_mixer_get_output_name(cras_output);

	if (first_internal_device(aio) && !has_node(aio, INTERNAL_SPEAKER))
		return INTERNAL_SPEAKER;
	else
		return "(default)";
}

static const char *get_input_node_name(struct alsa_io *aio)
{
	if (first_internal_device(aio) && !has_node(aio, INTERNAL_MICROPHONE))
		return INTERNAL_MICROPHONE;
	else
		return "(default)";
}

/* Callback for listing mixer outputs.  The mixer will call this once for each
 * output associated with this device.  Most commonly this is used to tell the
 * device it has Headphones and Speakers. */
static void new_output(struct cras_alsa_mixer_output *cras_output,
		       void *callback_arg)
{
	struct alsa_io *aio;
	struct alsa_output_node *output;
	const char *name;

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
	output->base.dev = &aio->base;
	output->base.idx = aio->next_ionode_index++;
	output->mixer_output = cras_output;
	name = get_output_node_name(aio, cras_output);
	strncpy(output->base.name, name, sizeof(output->base.name) - 1);
	set_node_initial_state(&output->base, aio->card_type);
	cras_iodev_add_node(&aio->base, &output->base);
}

static void new_input(struct alsa_io *aio)
{
	struct alsa_input_node *input;
	const char *name;

	input = (struct alsa_input_node *)calloc(1, sizeof(*input));
	if (input == NULL) {
		syslog(LOG_ERR, "Out of memory when listing inputs.");
		return;
	}
	input->base.dev = &aio->base;
	input->base.idx = aio->next_ionode_index++;
	name = get_input_node_name(aio);
	strncpy(input->base.name, name, sizeof(input->base.name) - 1);
	set_node_initial_state(&input->base, aio->card_type);
	cras_iodev_add_node(&aio->base, &input->base);
}

/* Finds the output node associated with the jack. Returns NULL if not found. */
static struct alsa_output_node *get_output_node_from_jack(
		struct alsa_io *aio, const struct cras_alsa_jack *jack)
{
	struct cras_alsa_mixer_output *mixer_output;
	struct cras_ionode *node = NULL;
	struct alsa_output_node *aout = NULL;

	mixer_output = cras_alsa_jack_get_mixer_output(jack);
	if (mixer_output == NULL) {
		/* no mixer output, search by node. */
		DL_SEARCH_SCALAR_WITH_CAST(aio->base.nodes, node, aout,
					   jack, jack);
		return aout;
	}

	DL_SEARCH_SCALAR_WITH_CAST(aio->base.nodes, node, aout,
				   mixer_output, mixer_output);
	return aout;
}

static struct alsa_input_node *get_input_node_from_jack(
		struct alsa_io *aio, const struct cras_alsa_jack *jack)
{
	struct mixer_volume_control *mixer_input;
	struct cras_ionode *node = NULL;
	struct alsa_input_node *ain = NULL;

	mixer_input = cras_alsa_jack_get_mixer_input(jack);
	if (mixer_input == NULL) {
		DL_SEARCH_SCALAR_WITH_CAST(aio->base.nodes, node, ain,
					   jack, jack);
		return ain;
	}

	DL_SEARCH_SCALAR_WITH_CAST(aio->base.nodes, node, ain,
				   mixer_input, mixer_input);
	return ain;
}

/* Returns the dsp name specified in the ucm config. If there is a dsp
 * name specified for the jack of the active node, use that. Otherwise
 * use the default dsp name for the alsa_io device. */
static const char *get_active_dsp_name(struct alsa_io *aio)
{
	struct cras_ionode *node = aio->base.active_node;
	const struct cras_alsa_jack *jack;

	if (node == NULL)
		return NULL;

	if (aio->base.direction == CRAS_STREAM_OUTPUT)
		jack = ((struct alsa_output_node *) node)->jack;
	else
		jack = ((struct alsa_input_node *) node)->jack;

	return cras_alsa_jack_get_dsp_name(jack) ? : aio->dsp_name_default;
}

/* Callback that is called when an output jack is plugged or unplugged. */
static void jack_output_plug_event(const struct cras_alsa_jack *jack,
				    int plugged,
				    void *arg)
{
	struct alsa_io *aio;
	struct alsa_output_node *node;
	const char *jack_name;

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
		node->base.dev = &aio->base;
		node->base.idx = aio->next_ionode_index++;
		jack_name = cras_alsa_jack_get_name(jack);
		node->jack_curve = cras_alsa_mixer_create_volume_curve_for_name(
				aio->mixer, jack_name);
		node->jack = jack;
		strncpy(node->base.name, jack_name,
			sizeof(node->base.name) - 1);
		set_node_initial_state(&node->base, aio->card_type);
		cras_iodev_add_node(&aio->base, &node->base);
	}

	cras_iodev_set_node_attr(&node->base, IONODE_ATTR_PLUGGED, plugged);
}

/* Callback that is called when an input jack is plugged or unplugged. */
static void jack_input_plug_event(const struct cras_alsa_jack *jack,
				  int plugged,
				  void *arg)
{
	struct alsa_io *aio;
	struct alsa_input_node *node;
	const char *jack_name;

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
		node->base.dev = &aio->base;
		node->base.idx = aio->next_ionode_index++;
		jack_name = cras_alsa_jack_get_name(jack);
		node->jack = jack;
		node->mixer_input = cras_alsa_jack_get_mixer_input(jack);
		strncpy(node->base.name, jack_name,
			sizeof(node->base.name) - 1);
		set_node_initial_state(&node->base, aio->card_type);
		cras_iodev_add_node(&aio->base, &node->base);
	}

	cras_iodev_set_node_attr(&node->base, IONODE_ATTR_PLUGGED, plugged);
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
				     enum CRAS_ALSA_CARD_TYPE card_type,
				     int is_first,
				     struct cras_alsa_mixer *mixer,
				     snd_use_case_mgr_t *ucm,
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
	aio->card_type = card_type;
	aio->is_first = is_first;
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
	iodev->update_active_node = update_active_node;
	if (card_type == ALSA_CARD_TYPE_USB)
		iodev->min_buffer_level = USB_EXTRA_BUFFER_FRAMES;

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
	if (ucm)
		aio->dsp_name_default = ucm_get_dsp_name_default(ucm,
								 direction);
	set_iodev_name(iodev, card_name, dev_name, card_index, device_index);

	/* Create output nodes for mixer controls, such as Headphone
	 * and Speaker. */
	if (direction == CRAS_STREAM_OUTPUT)
		cras_alsa_mixer_list_outputs(mixer, device_index, new_output,
					     aio);

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

	/* Create nodes for jacks that aren't associated with an
	 * already existing node. Get an initial read of the jacks for
	 * this device. */
	cras_alsa_jack_list_report(aio->jack_list);

	/* Make a default node if there is still no node for this
	 * device, or we still don't have the "Speaker"/"Internal Mic"
	 * node for the first internal device. */
	if (direction == CRAS_STREAM_OUTPUT) {
		if (!aio->base.nodes || (first_internal_device(aio) &&
					 !has_node(aio, INTERNAL_SPEAKER)))
			new_output(NULL, aio);
	} else {
		if (!aio->base.nodes || (first_internal_device(aio) &&
					 !has_node(aio, INTERNAL_MICROPHONE)))
			new_input(aio);
	}

	/* Set the active node as the best node we have now. */
	alsa_iodev_set_active_node(&aio->base,
				   cras_iodev_get_best_node(&aio->base));
	if (direction == CRAS_STREAM_OUTPUT)
		cras_iodev_list_add_output(&aio->base);
	else
		cras_iodev_list_add_input(&aio->base);

	/* Set plugged for the first USB device per card when it appears. */
	if (card_type == ALSA_CARD_TYPE_USB && is_first)
		cras_iodev_set_node_attr(iodev->active_node,
					 IONODE_ATTR_PLUGGED, 1);

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
	else
		rc = cras_iodev_list_rm_output(iodev);

	free_alsa_iodev_resources(aio);
	if (rc != -EBUSY)
		free(iodev);
}

static void alsa_iodev_unmute_node(struct alsa_io *aio,
				   struct cras_ionode *ionode)
{
	struct alsa_output_node *active = (struct alsa_output_node *)ionode;
	struct cras_alsa_mixer_output *mixer = active->mixer_output;
	struct alsa_output_node *output;
	struct cras_ionode *node;

	/* If this node is associated with mixer output, unmute the
	 * active mixer output and mute all others, otherwise just set
	 * the node as active and set the volume curve. */
	if (mixer) {
		set_alsa_mute(aio, 1);
		/* Unmute the active mixer output, mute all others. */
		DL_FOREACH(aio->base.nodes, node) {
			output = (struct alsa_output_node *)node;
			if (output->mixer_output)
				cras_alsa_mixer_set_output_active_state(
					output->mixer_output, node == ionode);
		}
	}
}

static void enable_jack_ucm(struct alsa_io *aio, int plugged)
{
	if (aio->base.direction == CRAS_STREAM_OUTPUT) {
		struct alsa_output_node *active = get_active_output(aio);
		if (active)
			cras_alsa_jack_enable_ucm(active->jack, plugged);
	} else {
		struct alsa_input_node *active = get_active_input(aio);
		if (active)
			cras_alsa_jack_enable_ucm(active->jack, plugged);
	}
}

int alsa_iodev_set_active_node(struct cras_iodev *iodev,
			       struct cras_ionode *ionode)
{
	struct alsa_io *aio = (struct alsa_io *)iodev;

	if (iodev->active_node == ionode)
		return 0;

	enable_jack_ucm(aio, 0);
	if (iodev->direction == CRAS_STREAM_OUTPUT)
		alsa_iodev_unmute_node(aio, ionode);

	cras_iodev_set_active_node(iodev, ionode);
	aio->base.dsp_name = get_active_dsp_name(aio);
	cras_iodev_update_dsp(iodev);
	enable_jack_ucm(aio, 1);
	/* Setting the volume will also unmute if the system isn't muted. */
	init_device_settings(aio);
	return 0;
}
