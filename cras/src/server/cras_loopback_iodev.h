/* Copyright (c) 2013 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef CRAS_LOOPBACK_IO_H_
#define CRAS_LOOPBACK_IO_H_

#include "cras_types.h"

struct cras_iodev;

/* Initializes loopback iodevs.  loopback iodevs provide the ability to
 * capture exactly what is being output by the system.
 */
void loopback_iodev_create(struct cras_iodev **loopback_input,
			   struct cras_iodev **loopback_output);

/* Destroys loopback_iodevs created with loopback_iodev_create. */
void loopback_iodev_destroy(struct cras_iodev *loopback_input,
			    struct cras_iodev *loopback_output);

/* Returns the number of frames to fill since last loopback output. */
unsigned int loopback_iodev_fill_level(struct cras_iodev *dev);

#endif /* CRAS_LOOPBACK_IO_H_ */
