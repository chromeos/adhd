/* Copyright 2013 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef CRAS_SRC_SERVER_CRAS_LOOPBACK_IODEV_H_
#define CRAS_SRC_SERVER_CRAS_LOOPBACK_IODEV_H_

#include "cras_types.h"

struct cras_iodev;

/* Initializes loopback iodevs.  loopback iodevs provide the ability to
 * capture exactly what is being output by the system.
 */
struct cras_iodev* loopback_iodev_create(enum CRAS_LOOPBACK_TYPE type);

// Destroys loopback_iodevs created with loopback_iodev_create.
void loopback_iodev_destroy(struct cras_iodev* loopdev);

#endif  // CRAS_LOOPBACK_IO_H_
