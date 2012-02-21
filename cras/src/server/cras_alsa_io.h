/* Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef CRAS_ALSA_IO_H_
#define CRAS_ALSA_IO_H_

#include "cras_types.h"

struct cras_alsa_mixer;

/* Initializes an alsa iodev.
 * Args:
 *    dev - the path to the alsa device to use.
 *    mixer - The mixer for the alsa device.
 *    direciton - input or output.
 * Returns:
 *    A pointer to the newly created iodev if successful, NULL otherwise.
 */
struct cras_iodev *alsa_iodev_create(const char *dev,
				     struct cras_alsa_mixer *mixer,
				     enum CRAS_STREAM_DIRECTION direction);

/* Destroys an alsa_iodev created with alsa_iodev_create. */
void alsa_iodev_destroy(struct cras_iodev *iodev);

#endif /* CRAS_ALSA_IO_H_ */
