/* Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <alsa/asoundlib.h>
#include <stdio.h>
#include <syslog.h>

#include "cras_alsa_mixer.h"
#include "cras_card_config.h"
#include "cras_util.h"
#include "cras_volume_curve.h"
#include "utlist.h"

/* Represents an ALSA control element. Each device can have several of these,
 * each potentially having independent volume and mute controls.
 * elem - ALSA mixer element.
 * has_volume - non-zero indicates there is a volume control.
 * has_mute - non-zero indicates there is a mute switch.
 */
struct mixer_control {
	snd_mixer_elem_t *elem;
	int has_volume;
	int has_mute;
	struct mixer_control *prev, *next;
};

/* Represents an ALSA control element related to a specific output such as
 * speakers or headphones.  A device can have several of these, each potentially
 * having independent volume and mute controls.
 * max_volume_dB - Maximum volume available in the volume control.
 * min_volume_dB - Minimum volume available in the volume control.
 * volume_curve - Curve for this output.
 */
struct mixer_output_control {
	struct mixer_control base;
	long max_volume_dB;
	long min_volume_dB;
	struct cras_volume_curve *volume_curve;
};

/* Holds a reference to the opened mixer and the volume controls.
 * mixer - Pointer to the opened alsa mixer.
 * main_volume_controls - List of volume controls (normally 'Master' and 'PCM').
 * playback_switch - Switch used to mute the device.
 * main_capture_controls - List of capture gain controls (normally 'Capture').
 * capture_switch - Switch used to mute the capture stream.
 * volume_curve - Default volume curve that converts from an index to dBFS.
 * max_volume_dB - Maximum volume available in main volume controls.  The dBFS
 *   value setting will be applied relative to this.
 * min_volume_dB - Minimum volume available in main volume controls.
 * config - Config info for this card, can be NULL if none found.
 */
struct cras_alsa_mixer {
	snd_mixer_t *mixer;
	struct mixer_control *main_volume_controls;
	struct mixer_control *output_controls;
	snd_mixer_elem_t *playback_switch;
	struct mixer_control *main_capture_controls;
	struct mixer_control *input_controls;
	snd_mixer_elem_t *capture_switch;
	struct cras_volume_curve *volume_curve;
	long max_volume_dB;
	long min_volume_dB;
	const struct cras_card_config *config;
};

/* Wrapper for snd_mixer_open and helpers.
 * Args:
 *    mixdev - Name of the device to open the mixer for.
 *    mixer - Pointer filled with the opened mixer on success, NULL on failure.
 */
static void alsa_mixer_open(const char *mixdev,
			    snd_mixer_t **mixer)
{
	int rc;

	*mixer = NULL;
	rc = snd_mixer_open(mixer, 0);
	if (rc < 0)
		return;
	rc = snd_mixer_attach(*mixer, mixdev);
	if (rc < 0)
		goto fail_after_open;
	rc = snd_mixer_selem_register(*mixer, NULL, NULL);
	if (rc < 0)
		goto fail_after_open;
	rc = snd_mixer_load(*mixer);
	if (rc < 0)
		goto fail_after_open;
	return;

fail_after_open:
	snd_mixer_close(*mixer);
	*mixer = NULL;
}

/* Checks if the given element's name is in the list. */
static int name_in_list(const char *name,
			const char * const list[],
			size_t len)
{
	size_t i;

	for (i = 0; i < len; i++)
		if (list[i] && strcmp(list[i], name) == 0)
			return 1;
	return 0;
}

/* Adds the main volume control to the list and grabs the first seen playback
 * switch to use for mute. */
static int add_main_volume_control(struct cras_alsa_mixer *cmix,
				   snd_mixer_elem_t *elem)
{
	if (snd_mixer_selem_has_playback_volume(elem)) {
		struct mixer_control *c, *next;
		long min, max;

		c = (struct mixer_control *)calloc(1, sizeof(*c));
		if (c == NULL) {
			syslog(LOG_ERR, "No memory for mixer.");
			return -ENOMEM;
		}

		c->elem = elem;

		if (snd_mixer_selem_get_playback_dB_range(elem,
							  &min,
							  &max) == 0) {
			cmix->max_volume_dB += max;
			cmix->min_volume_dB += min;
		}

		DL_FOREACH(cmix->main_volume_controls, next) {
			long next_min, next_max;
			snd_mixer_selem_get_playback_dB_range(next->elem,
							      &next_min,
							      &next_max);
			if (max - min > next_max - next_min)
				break;
		}

		DL_INSERT(cmix->main_volume_controls, next, c);
	}

	/* If cmix doesn't yet have a playback switch and this is a playback
	 * switch, use it. */
	if (cmix->playback_switch == NULL &&
			snd_mixer_selem_has_playback_switch(elem))
		cmix->playback_switch = elem;

	return 0;
}

/* Adds the main capture control to the list and grabs the first seen capture
 * switch to mute input. */
static int add_main_capture_control(struct cras_alsa_mixer *cmix,
				    snd_mixer_elem_t *elem)
{
	/* TODO(dgreid) handle index != 0, map to correct input. */
	if (snd_mixer_selem_get_index(elem) > 0)
		return 0;

	if (snd_mixer_selem_has_capture_volume(elem)) {
		struct mixer_control *c;

		c = (struct mixer_control *)calloc(1, sizeof(*c));
		if (c == NULL) {
			syslog(LOG_ERR, "No memory for control.");
			return -ENOMEM;
		}

		c->elem = elem;

		syslog(LOG_DEBUG,
		       "Add capture control %s\n",
		       snd_mixer_selem_get_name(elem));
		DL_APPEND(cmix->main_capture_controls, c);
	}

	/* If cmix doesn't yet have a capture switch and this is a capture
	 * switch, use it. */
	if (cmix->capture_switch == NULL &&
	    snd_mixer_selem_has_capture_switch(elem))
		cmix->capture_switch = elem;

	return 0;
}

/* Creates a volume curve for a new output. */
static struct cras_volume_curve *create_volume_curve_for_output(
		const struct cras_alsa_mixer *cmix,
		snd_mixer_elem_t *elem)
{
	const char *output_name;

	output_name = snd_mixer_selem_get_name(elem);
	return cras_card_config_get_volume_curve_for_control(cmix->config,
							     output_name);
}

/* Adds an output control to the list. */
static int add_output_control(struct cras_alsa_mixer *cmix,
			      snd_mixer_elem_t *elem)
{
	int index; /* Index part of mixer simple element */
	struct mixer_control *c;
	struct mixer_output_control *output;
	long min, max;

	index = snd_mixer_selem_get_index(elem);
	syslog(LOG_DEBUG, "Add output control: %s,%d\n",
	       snd_mixer_selem_get_name(elem), index);

	output = (struct mixer_output_control *)calloc(1, sizeof(*output));
	if (output == NULL) {
		syslog(LOG_ERR, "No memory for output control.");
		return -ENOMEM;
	}

	if (snd_mixer_selem_get_playback_dB_range(elem, &min, &max) == 0) {
		output->max_volume_dB = max;
		output->min_volume_dB = min;
	}
	output->volume_curve = create_volume_curve_for_output(cmix, elem);

	c = &output->base;
	c->elem = elem;
	c->has_volume = snd_mixer_selem_has_playback_volume(elem);
	c->has_mute = snd_mixer_selem_has_playback_switch(elem);
	DL_APPEND(cmix->output_controls, c);

	return 0;
}

/* Adds an input control to the list. */
static int add_input_control(struct cras_alsa_mixer *cmix,
			      snd_mixer_elem_t *elem)
{
	int index; /* Index part of mixer simple element */
	struct mixer_control *c;

	index = snd_mixer_selem_get_index(elem);
	syslog(LOG_DEBUG, "Add input control: %s,%d\n",
	       snd_mixer_selem_get_name(elem), index);

	c = (struct mixer_control *)calloc(1, sizeof(*c));
	if (c == NULL) {
		syslog(LOG_ERR, "No memory for input control.");
		return -ENOMEM;
	}

	c->elem = elem;
	c->has_volume = snd_mixer_selem_has_capture_volume(elem);
	c->has_mute = snd_mixer_selem_has_capture_switch(elem);
	DL_APPEND(cmix->input_controls, c);

	return 0;
}

static void list_controls(struct mixer_control *control_list,
			  cras_alsa_mixer_control_callback cb,
			  void *cb_arg)
{
	struct mixer_control *control;

	DL_FOREACH(control_list, control)
		cb(control, cb_arg);
}

static struct mixer_control *get_control_matching_name(
		struct mixer_control *control_list,
		const char *name)
{
	struct mixer_control *c;

	DL_FOREACH(control_list, c) {
		const char *elem_name;

		elem_name = snd_mixer_selem_get_name(c->elem);
		if (elem_name == NULL)
			continue;
		if (strstr(name, elem_name))
			return c;
	}
	return NULL;
}

/*
 * Exported interface.
 */

struct cras_alsa_mixer *cras_alsa_mixer_create(
		const char *card_name,
		const struct cras_card_config *config,
		const char *output_names_extra[],
		size_t output_names_extra_size,
		const char *extra_main_volume)
{
	/* Names of controls for main system volume. */
	static const char * const main_volume_names[] = {
		"Master",
		"Digital",
		"PCM",
	};
	/* Names of controls for individual outputs. */
	static const char * const output_names[] = {
		"Headphone",
		"Headset",
		"HDMI",
		"Speaker",
	};
	/* Names of controls for capture gain/attenuation and mute. */
	static const char * const main_capture_names[] = {
		"Capture",
		"Digital Capture",
	};
	/* Names of controls for individual inputs. */
	static const char * const input_names[] = {
		"Mic",
		"Microphone",
	};
	snd_mixer_elem_t *elem;
	struct cras_alsa_mixer *cmix;
	snd_mixer_elem_t *other_elem = NULL;
	long other_dB_range = 0;

	cmix = (struct cras_alsa_mixer *)calloc(1, sizeof(*cmix));
	if (cmix == NULL)
		return NULL;

	syslog(LOG_DEBUG, "Add mixer for device %s", card_name);

	cmix->config = config;
	cmix->volume_curve =
		cras_card_config_get_volume_curve_for_control(cmix->config,
							      "Default");

	alsa_mixer_open(card_name, &cmix->mixer);
	if (cmix->mixer == NULL) {
		syslog(LOG_DEBUG, "Couldn't open mixer.");
		return cmix;
	}

	/* Find volume and mute controls. */
	for(elem = snd_mixer_first_elem(cmix->mixer);
			elem != NULL; elem = snd_mixer_elem_next(elem)) {
		const char *name;

		name = snd_mixer_selem_get_name(elem);
		if (name == NULL)
			continue;

		if (!extra_main_volume &&
		    name_in_list(name, main_volume_names,
				 ARRAY_SIZE(main_volume_names))) {
			if (add_main_volume_control(cmix, elem) != 0) {
				cras_alsa_mixer_destroy(cmix);
				return NULL;
			}
		} else if (name_in_list(name, main_capture_names,
					ARRAY_SIZE(main_capture_names))) {
			if (add_main_capture_control(cmix, elem) != 0) {
				cras_alsa_mixer_destroy(cmix);
				return NULL;
			}
		} else if (name_in_list(name, output_names,
					ARRAY_SIZE(output_names))
			   || name_in_list(name, output_names_extra,
					   output_names_extra_size)) {
			/* TODO(dgreid) - determine device index. */
			if (add_output_control(cmix, elem) != 0) {
				cras_alsa_mixer_destroy(cmix);
				return NULL;
			}
		} else if (name_in_list(name, input_names,
					ARRAY_SIZE(input_names))) {
			if (add_input_control(cmix, elem) != 0) {
				cras_alsa_mixer_destroy(cmix);
				return NULL;
			}
		} else if (extra_main_volume &&
			   !strcmp(name, extra_main_volume)) {
			if (add_main_volume_control(cmix, elem) != 0) {
				cras_alsa_mixer_destroy(cmix);
				return NULL;
			}
		} else if (snd_mixer_selem_has_playback_volume(elem)) {
			/* Temporarily cache one elem whose name is not
			 * in the list above, but has a playback volume
			 * control and the largest volume range. */
			long min, max, range;
			if (snd_mixer_selem_get_playback_dB_range(elem,
								  &min,
								  &max) != 0)
				continue;

			range = max - min;
			if (other_dB_range < range) {
				other_dB_range = range;
				other_elem = elem;
			}
		}
	}

	/* If there is no volume control and output control found,
	 * use the volume control which has the largest volume range
	 * in the mixer as a main volume control. */
	if (!cmix->main_volume_controls && !cmix->output_controls &&
	    other_elem) {
		if (add_main_volume_control(cmix, other_elem) != 0) {
			cras_alsa_mixer_destroy(cmix);
			return NULL;
		}
	}

	return cmix;
}

void cras_alsa_mixer_destroy(struct cras_alsa_mixer *cras_mixer)
{
	struct mixer_control *c;

	assert(cras_mixer);

	DL_FOREACH(cras_mixer->main_volume_controls, c) {
		DL_DELETE(cras_mixer->main_volume_controls, c);
		free(c);
	}
	DL_FOREACH(cras_mixer->main_capture_controls, c) {
		DL_DELETE(cras_mixer->main_capture_controls, c);
		free(c);
	}
	DL_FOREACH(cras_mixer->output_controls, c) {
		struct mixer_output_control *output;
		output = (struct mixer_output_control *)c;
		cras_volume_curve_destroy(output->volume_curve);
		DL_DELETE(cras_mixer->output_controls, c);
		free(output);
	}
	DL_FOREACH(cras_mixer->input_controls, c) {
		DL_DELETE(cras_mixer->input_controls, c);
		free(c);
	}
	cras_volume_curve_destroy(cras_mixer->volume_curve);
	if (cras_mixer->mixer)
		snd_mixer_close(cras_mixer->mixer);
	free(cras_mixer);
}

const struct cras_volume_curve *cras_alsa_mixer_default_volume_curve(
		const struct cras_alsa_mixer *cras_mixer)
{
	assert(cras_mixer);
	assert(cras_mixer->volume_curve);
	return cras_mixer->volume_curve;
}

void cras_alsa_mixer_set_dBFS(struct cras_alsa_mixer *cras_mixer,
			      long dBFS,
			      struct mixer_control *mixer_output)
{
	struct mixer_control *c;
	struct mixer_output_control *output;
	output = (struct mixer_output_control *)mixer_output;
	long to_set;

	assert(cras_mixer);

	/* dBFS is normally < 0 to specify the attenuation from max. max is the
	 * combined max of the master controls and the current output.
	 */
	to_set = dBFS + cras_mixer->max_volume_dB;
	if (mixer_output)
		to_set += output->max_volume_dB;
	/* Go through all the controls, set the volume level for each,
	 * taking the value closest but greater than the desired volume.  If the
	 * entire volume can't be set on the current control, move on to the
	 * next one until we have the exact volume, or gotten as close as we
	 * can. Once all of the volume is set the rest of the controls should be
	 * set to 0dB. */
	DL_FOREACH(cras_mixer->main_volume_controls, c) {
		long actual_dB;
		snd_mixer_selem_set_playback_dB_all(c->elem, to_set, 1);
		snd_mixer_selem_get_playback_dB(c->elem,
						SND_MIXER_SCHN_FRONT_LEFT,
						&actual_dB);
		to_set -= actual_dB;
	}
	/* Apply the rest to the output-specific control. */
	if (mixer_output && mixer_output->elem && mixer_output->has_volume)
		snd_mixer_selem_set_playback_dB_all(mixer_output->elem,
						    to_set,
						    1);
}

long cras_alsa_mixer_get_dB_range(struct cras_alsa_mixer *cras_mixer)
{
	if (!cras_mixer)
		return 0;
	return cras_mixer->max_volume_dB - cras_mixer->min_volume_dB;
}

long cras_alsa_mixer_get_output_dB_range(
		struct mixer_control *mixer_output)
{
	struct mixer_output_control *output;
	if (!mixer_output || !mixer_output->elem || !mixer_output->has_volume)
		return 0;
	output = (struct mixer_output_control *)mixer_output;
	return output->max_volume_dB - output->min_volume_dB;
}

void cras_alsa_mixer_set_capture_dBFS(struct cras_alsa_mixer *cras_mixer,
				      long dBFS,
				      struct mixer_control *mixer_input)
{
	struct mixer_control *c;
	long to_set;

	assert(cras_mixer);
	to_set = dBFS;
	/* Go through all the controls, set the gain for each, taking the value
	 * closest but greater than the desired gain.  If the entire gain can't
	 * be set on the current control, move on to the next one until we have
	 * the exact gain, or gotten as close as we can. Once all of the gain is
	 * set the rest of the controls should be set to 0dB. */
	DL_FOREACH(cras_mixer->main_capture_controls, c) {
		long actual_dB;
		snd_mixer_selem_set_capture_dB_all(c->elem, to_set, 1);
		snd_mixer_selem_get_capture_dB(c->elem,
					       SND_MIXER_SCHN_FRONT_LEFT,
					       &actual_dB);
		to_set -= actual_dB;
	}

	/* Apply the reset to input specific control */
	if (mixer_input && mixer_input->elem && mixer_input->has_volume)
		snd_mixer_selem_set_capture_dB_all(mixer_input->elem,
						   to_set, 1);
	assert(cras_mixer);
}

long cras_alsa_mixer_get_minimum_capture_gain(
                struct cras_alsa_mixer *cmix,
		struct mixer_control *mixer_input)
{
	struct mixer_control *c;
	long min, max, total_min;

	assert(cmix);
	total_min = 0;
	DL_FOREACH(cmix->main_capture_controls, c)
		if (snd_mixer_selem_get_capture_dB_range(c->elem,
							 &min, &max) == 0)
			total_min += min;

	if (mixer_input && snd_mixer_selem_get_capture_dB_range(
			mixer_input->elem, &min, &max) == 0)
		total_min += min;

	return total_min;
}

long cras_alsa_mixer_get_maximum_capture_gain(struct cras_alsa_mixer *cmix,
		struct mixer_control *mixer_input)
{
	struct mixer_control *c;
	long min, max, total_max;

	assert(cmix);
	total_max = 0;
	DL_FOREACH(cmix->main_capture_controls, c)
		if (snd_mixer_selem_get_capture_dB_range(c->elem,
							 &min, &max) == 0)
			total_max += max;

	if (mixer_input && snd_mixer_selem_get_capture_dB_range(
			mixer_input->elem, &min, &max) == 0)
		total_max += max;

	return total_max;
}

void cras_alsa_mixer_set_mute(struct cras_alsa_mixer *cras_mixer,
			      int muted,
			      struct mixer_control *mixer_output)
{
	assert(cras_mixer);
	if (cras_mixer->playback_switch) {
		snd_mixer_selem_set_playback_switch_all(
				cras_mixer->playback_switch, !muted);
		return;
	}
	if (mixer_output && mixer_output->has_mute)
		snd_mixer_selem_set_playback_switch_all(
				mixer_output->elem, !muted);
}

void cras_alsa_mixer_set_capture_mute(struct cras_alsa_mixer *cras_mixer,
				      int muted,
				      struct mixer_control *mixer_input)
{
	assert(cras_mixer);
	if (cras_mixer->capture_switch) {
		snd_mixer_selem_set_capture_switch_all(
				cras_mixer->capture_switch, !muted);
		return;
	}
	if (mixer_input && mixer_input->has_mute)
		snd_mixer_selem_set_capture_switch_all(
				mixer_input->elem, !muted);
}

void cras_alsa_mixer_list_outputs(struct cras_alsa_mixer *cras_mixer,
				  cras_alsa_mixer_control_callback cb,
				  void *cb_arg)
{
	assert(cras_mixer);
	list_controls(cras_mixer->output_controls, cb, cb_arg);
}

void cras_alsa_mixer_list_inputs(struct cras_alsa_mixer *cras_mixer,
				 cras_alsa_mixer_control_callback cb,
				 void *cb_arg)
{
	assert(cras_mixer);
	list_controls(cras_mixer->input_controls, cb, cb_arg);
}

const char *cras_alsa_mixer_get_control_name(
		const struct mixer_control *control)
{
	return snd_mixer_selem_get_name(control->elem);
}

struct mixer_control *cras_alsa_mixer_get_output_matching_name(
		const struct cras_alsa_mixer *cras_mixer,
		const char * const name)
{
	assert(cras_mixer);
	return get_control_matching_name(cras_mixer->output_controls, name);
}

struct mixer_control *cras_alsa_mixer_get_input_matching_name(
		struct cras_alsa_mixer *cras_mixer,
		const char *name)
{
	struct mixer_control *c = NULL;
	snd_mixer_elem_t *elem;

	assert(cras_mixer);
	c = get_control_matching_name(cras_mixer->input_controls, name);
	if (c)
		return c;

	if (!cras_mixer->mixer)
		return NULL;

	/* TODO: This is a workaround, we should pass the input names in
	 * ucm config to cras_alsa_mixer_create. */
	for (elem = snd_mixer_first_elem(cras_mixer->mixer);
			elem != NULL; elem = snd_mixer_elem_next(elem)) {
		const char *control_name;
		control_name = snd_mixer_selem_get_name(elem);

		if (control_name == NULL)
			continue;
		if (strcmp(name, control_name) == 0) {
			if (add_input_control(cras_mixer, elem) == 0)
				return cras_mixer->input_controls->prev;
		}
	}
	return NULL;
}

int cras_alsa_mixer_set_output_active_state(
		struct mixer_control *output,
		int active)
{
	assert(output);
	if (!output->has_mute)
		return -1;
	return snd_mixer_selem_set_playback_switch_all(output->elem, active);
}

struct cras_volume_curve *cras_alsa_mixer_create_volume_curve_for_name(
		const struct cras_alsa_mixer *cmix,
		const char *name)
{
	if (cmix != NULL)
		return cras_card_config_get_volume_curve_for_control(
				cmix->config, name);
	else
		return cras_card_config_get_volume_curve_for_control(NULL,
								     name);
}

struct cras_volume_curve *cras_alsa_mixer_get_output_volume_curve(
		const struct mixer_control *control)
{
	struct mixer_output_control *output;
	output = (struct mixer_output_control *)control;
	if (output)
		return output->volume_curve;
	else
		return NULL;
}
