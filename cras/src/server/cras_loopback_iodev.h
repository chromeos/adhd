/* Copyright (c) 2013 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef CRAS_LOOPBACK_IO_H_
#define CRAS_LOOPBACK_IO_H_

#include "cras_types.h"

struct cras_iodev;

/* Initializes an loopback iodev.  loopback iodevs provide the ability to
 * capture exactly what is being output by the system.  Currently
 * CRAS_STREAM_POST_MIX_PRE_DSP is the only direction supported, it provides the
 * samples to be output after they are mixed.
 * Args:
 *    direciton - input or output.
 * Returns:
 *    A pointer to the newly created iodev if successful, NULL otherwise.
 */
struct cras_iodev *loopback_iodev_create(enum CRAS_STREAM_DIRECTION direction);

/* Destroys an loopback_iodev created with loopback_iodev_create. */
void loopback_iodev_destroy(struct cras_iodev *iodev);

/* Supplies samples to be looped back.
 * Args:
 *    loopback_dev - The loopback device.
 *    audio - The samples to write, fill with zeros if NULL.
 *    count - Number of frames to write.
 */
int loopback_iodev_add_audio(struct cras_iodev *dev,
			     const uint8_t *audio,
			     unsigned int count);

/* Supplies zeros to be looped back. Use when no output streams are active.
 * Args:
 *    loopback_dev - The loopback device.
 *    count - Number of frames of zerosto write.
 */
int loopback_iodev_add_zeros(struct cras_iodev *dev,
			     unsigned int count);

/* Set the format used for the loopback device.  This is set to match the output
 * that is being looped back. */
void loopback_iodev_set_format(struct cras_iodev *loopback_dev,
			       const struct cras_audio_format *fmt);

#endif /* CRAS_LOOPBACK_IO_H_ */
