/* Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef CRAS_ALSA_IO_H_
#define CRAS_ALSA_IO_H_

#include "cras_types.h"

struct cras_alsa_mixer;
struct cras_alsa_mixer_output;

/* Initializes an alsa iodev.
 * Args:
 *    dev - the path to the alsa device to use.
 *    mixer - The mixer for the alsa device.
 *    auto_route - If non-zero make this new device the default.
 *    direciton - input or output.
 * Returns:
 *    A pointer to the newly created iodev if successful, NULL otherwise.
 */
struct cras_iodev *alsa_iodev_create(size_t card_index,
				     size_t device_index,
				     struct cras_alsa_mixer *mixer,
				     int auto_route,
				     enum CRAS_STREAM_DIRECTION direction);

/* Destroys an alsa_iodev created with alsa_iodev_create. */
void alsa_iodev_destroy(struct cras_iodev *iodev);

/* Sets the active output of an alsa mixer.  Used to switch form Speaker to
 * Headphones or vice-versa.
 * Args:
 *    iodev - An iodev created with alsa_iodev_create.
 *    active - The output to activate.
 */
int alsa_iodev_set_active_output(struct cras_iodev *iodev,
				 struct cras_alsa_mixer_output *active);

#endif /* CRAS_ALSA_IO_H_ */
