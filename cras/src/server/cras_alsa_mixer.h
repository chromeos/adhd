/* Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef _CRAS_ALSA_MIXER_H
#define _CRAS_ALSA_MIXER_H

#include <alsa/asoundlib.h>

/* cras_alsa_mixer represents the alsa mixer interface for an alsa card.  It
 * houses the volume and mute controls as well as playback switches for
 * headphones and mic.
 */

struct cras_alsa_mixer;

struct cras_alsa_mixer_output {
	snd_mixer_elem_t *elem; /* ALSA mixer element. */
	int has_volume; /* non-zero indicates there is a volume control. */
	int has_mute; /* non-zero indicates there is a mute switch. */
	size_t device_index; /* ALSA device index for this control. */
};

/* Creates a cras_alsa_mixer instance for the given alsa device.
 * Args:
 *    card_name - Name of the card to open a mixer for.  This is an alsa name of
 *    the form "hw:X" where X ranges from 0 to 31 inclusive.
 * Returns:
 *    A pointer to the newly created cras_alsa_mixer which must later be freed
 *    by calling cras_alsa_mixer_destroy.
 */
struct cras_alsa_mixer *cras_alsa_mixer_create(const char *card_name);

/* Destroys a cras_alsa_mixer that was returned from cras_alsa_mixer_create.
 * Args:
 *    cras_mixer - The cras_alsa_mixer pointer returned from
 *        cras_alsa_mixer_create.
 */
void cras_alsa_mixer_destroy(struct cras_alsa_mixer *cras_mixer);

/* Sets the output volume for the device associated with this mixer.
 * Args:
 *    cras_mixer - The mixer to set the volume on.
 *    dBFS - The volume level as dB * 100.  dB is a normally a negative quantity
 *      specifying how much to attenuate.
 */
void cras_alsa_mixer_set_dBFS(struct cras_alsa_mixer *cras_mixer,
				long dBFS);

/* Sets the playback switch for the device.
 * Args:
 *    cras_mixer - Mixer to set the volume in.
 *    muted - 1 if muted, 0 if not.
 */
void cras_alsa_mixer_set_mute(struct cras_alsa_mixer *cras_mixer, int muted);

/* Invokes the provided callback once for each output associated with the given
 * device number.  The callback will be provided with a reference to the control
 * that can be queried to see what the control supports.
 * Args:
 *    cras_mixer - Mixer to set the volume in.
 *    device_index - Y in hw:X,Y.
 *    cb - Function to call for each output.
 *    cb_arg - Argument to pass to cb.
 */
typedef void (*cras_alsa_mixer_output_callback)(
		struct cras_alsa_mixer_output *output, void *arg);
void cras_alsa_mixer_list_outputs(struct cras_alsa_mixer *cras_mixer,
				  size_t device_index,
				  cras_alsa_mixer_output_callback cb,
				  void *cb_arg);

/* Sets the given output active or inactive. */
int cras_alsa_mixer_set_output_active_state(
		struct cras_alsa_mixer_output *output,
		int active);

#endif /* _CRAS_ALSA_MIXER_H */
