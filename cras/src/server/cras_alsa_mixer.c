/* Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <alsa/asoundlib.h>
#include <stdio.h>
#include <syslog.h>

#include "cras_alsa_mixer.h"
#include "cras_util.h"
#include "utlist.h"

/* Alsa names of the form "hw:X". */
#define MAX_ALSA_PCM_NAME_LENGTH 5

/* Control names to look for to control the main volume of an alsa device. */
static const char * const main_volume_control_names[] = {
	"Master",
	"Digital",
	"PCM"
};

/* Represents an ALSA volume control element. Each device can have several
 * volume controls in the path to the output, a list of these will be used to
 * represent that so we can used each volume control in sequence. */
struct mixer_volume_control {
	snd_mixer_elem_t *elem;
	struct mixer_volume_control *prev, *next;
};

/* Holds a reference to the opened mixer and the volume controls.
 * mixer - Pointer to the opened alsa mixer.
 * name - Of the owning device (e.g. "hw:0").
 * main_volume_controls - List of volume controls (normally 'Master' and 'PCM').
 */
struct cras_alsa_mixer {
	snd_mixer_t *mixer;
	char name[MAX_ALSA_PCM_NAME_LENGTH + 1];
	struct mixer_volume_control *main_volume_controls;
};

/* Wrapper for snd_mixer_open and helpers.
 * Args:
 *    mixdev - Name of the device to open the mixer for.
 * Returns:
 *    pointer to opened mixer on success, NULL on failure.
 */
static snd_mixer_t *alsa_mixer_open(const char *mixdev)
{
	snd_mixer_t *mixer = NULL;
	int rc;
	rc = snd_mixer_open(&mixer, 0);
	if (rc < 0)
		return NULL;
	rc = snd_mixer_attach(mixer, mixdev);
	if (rc < 0)
		goto fail_after_open;
	rc = snd_mixer_selem_register(mixer, NULL, NULL);
	if (rc < 0)
		goto fail_after_open;
	rc = snd_mixer_load(mixer);
	if (rc < 0)
		goto fail_after_open;
	return mixer;

fail_after_open:
	snd_mixer_close(mixer);
	return NULL;
}

/* Checks if the given element is one of the main mixer controls by
 * matching against the standard control names. */
static int is_main_volume_control(snd_mixer_elem_t *elem)
{
	const char *name;
	size_t i;

	name = snd_mixer_selem_get_name(elem);
	for (i = 0; i < ARRAY_SIZE(main_volume_control_names); i++) {
		if (strcmp(main_volume_control_names[i], name) == 0) {
			syslog(LOG_DEBUG, "- Add volume control %s.", name);
			return 1;
		}
	}
	return 0;
}

/*
 * Exported interface.
 */

struct cras_alsa_mixer *cras_alsa_mixer_create(int card_index)
{
	snd_mixer_elem_t *elem;
	struct cras_alsa_mixer *cmix;

	assert(card_index < 32); /* ALSA supports up to 32 cards. */

	cmix = calloc(1, sizeof(*cmix));
	if (cmix == NULL)
		return NULL;

	snprintf(cmix->name, MAX_ALSA_PCM_NAME_LENGTH, "hw:%d", card_index);
	syslog(LOG_DEBUG, "Add mixer for device %s", cmix->name);

	cmix->mixer = alsa_mixer_open(cmix->name);
	if (cmix->mixer == NULL) {
		syslog(LOG_DEBUG, "Couldn't open mixer.");
		return NULL;
	}

	elem = snd_mixer_first_elem(cmix->mixer);
	while (elem != NULL) {
		if (snd_mixer_selem_has_playback_volume(elem) &&
		    is_main_volume_control(elem)) {
			struct mixer_volume_control *c;

			c = calloc(1, sizeof(*c));
			if (c == NULL) {
				cras_alsa_mixer_destroy(cmix);
				syslog(LOG_ERR, "No memory for mixer.");
				return NULL;
			}

			c->elem = elem;

			DL_APPEND(cmix->main_volume_controls, c);
		}
		elem = snd_mixer_elem_next(elem);
	}

	return cmix;
}

void cras_alsa_mixer_destroy(struct cras_alsa_mixer *cras_mixer)
{
	struct mixer_volume_control *c, *tmp;

	assert(cras_mixer);

	DL_FOREACH_SAFE(cras_mixer->main_volume_controls, c, tmp) {
		DL_DELETE(cras_mixer->main_volume_controls, c);
		free(c);
	}
	snd_mixer_close(cras_mixer->mixer);
	free(cras_mixer);
}

void cras_alsa_mixer_set_volume(struct cras_alsa_mixer *cras_mixer,
				long volume_dB)
{
	struct mixer_volume_control *c;
	long to_set;

	assert(cras_mixer);

	to_set = volume_dB;
	/* Go through all the controls, set the volume level for each,
	 * taking the value closest but greater than the desired volume.  If the
	 * entire volume can't be set on the current control, move on to the
	 * next one until we have the exact volume, or gotten as close as we
	 * can. */
	DL_FOREACH(cras_mixer->main_volume_controls, c) {
		long actual_dB;
		snd_mixer_selem_set_playback_dB_all(c->elem, to_set, 1);
		snd_mixer_selem_get_playback_dB(c->elem,
						SND_MIXER_SCHN_FRONT_LEFT,
						&actual_dB);
		to_set -= actual_dB;
		if (to_set == 0)
			return;
	}
}
