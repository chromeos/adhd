/* Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef CRAS_ALSA_IO_H_
#define CRAS_ALSA_IO_H_

#include <alsa/asoundlib.h>
#include <alsa/use-case.h>

#include "cras_types.h"

struct cras_alsa_mixer;
struct cras_alsa_mixer_output;
struct cras_ionode;

/* Initializes an alsa iodev.
 * Args:
 *    card_index - 0 based index, value of "XX" in "hw:XX,YY".
 *    card_name - The name of the card.
 *    device_index - 0 based index, value of "YY" in "hw:XX,YY".
 *    dev_name - The name of the device.
 *    card_type - the type of the card this iodev belongs.
 *    is_first - if this is the first iodev on the card.
 *    mixer - The mixer for the alsa device.
 *    ucm - ALSA use case manager if available.
 *    direciton - input or output.
 * Returns:
 *    A pointer to the newly created iodev if successful, NULL otherwise.
 */
struct cras_iodev *alsa_iodev_create(size_t card_index,
				     const char *card_name,
				     size_t device_index,
				     const char *dev_name,
				     enum CRAS_ALSA_CARD_TYPE card_type,
				     int is_first,
				     struct cras_alsa_mixer *mixer,
				     snd_use_case_mgr_t *ucm,
				     enum CRAS_STREAM_DIRECTION direction);

/* Destroys an alsa_iodev created with alsa_iodev_create. */
void alsa_iodev_destroy(struct cras_iodev *iodev);

/* Sets the active output of an alsa mixer.  Used to switch form Speaker to
 * Headphones or vice-versa.
 * Args:
 *    iodev - An iodev created with alsa_iodev_create.
 *    ionode - The output to activate.
 */
int alsa_iodev_set_active_output(struct cras_iodev *iodev,
				 struct cras_ionode *ionode);

/* Sets the active input of an alsa mixer.  Used to switch between different
 * Microphones.
 * Args:
 *    iodev - An iodev created with alsa_iodev_create.
 *    ionode - The input to activate.
 */
int alsa_iodev_set_active_input(struct cras_iodev *iodev,
				struct cras_ionode *ionode);

#endif /* CRAS_ALSA_IO_H_ */
