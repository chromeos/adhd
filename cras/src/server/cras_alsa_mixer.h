/* Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef _CRAS_ALSA_MIXER_H
#define _CRAS_ALSA_MIXER_H

/* cras_alsa_mixer represents the alsa mixer interface for an alsa card.  It
 * houses the volume and mute controls as well as playback switches for
 * headphones and mic.
 */

struct cras_alsa_mixer;

/* Creates a cras_alsa_mixer instance for the given alsa device.
 * Args:
 *    card_index - Index of the card to open a mixer for.
 * Returns:
 *    A pointer to the newly created cras_alsa_mixer which must later be freed
 *    by calling cras_alsa_mixer_destroy.
 */
struct cras_alsa_mixer *cras_alsa_mixer_create(int card_index);

/* Destroys a cras_alsa_mixer that was returned from cras_alsa_mixer_create.
 * Args:
 *    cras_mixer - The cras_alsa_mixer pointer returned from
 *        cras_alsa_mixer_create.
 */
void cras_alsa_mixer_destroy(struct cras_alsa_mixer *cras_mixer);

/* Sets the output volume for the device associated with this mixer.
 * Args:
 *    cras_mixer - The mixer to set the volume on.
 *    volume_dB - The volume level as dB * 100.
 */
void cras_alsa_mixer_set_volume(struct cras_alsa_mixer *cras_mixer,
				long volume_dB);

/* Sets the playback switch for the device.
 * Args:
 *    cras_mixer - Mixer to set the volume in.
 *    muted - 1 if muted, 0 if not.
 */
void cras_alsa_mixer_set_mute(struct cras_alsa_mixer *cras_mixer, int muted);

#endif /* _CRAS_ALSA_MIXER_H */
