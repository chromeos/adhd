/* Copyright (c) 2014 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef CRAS_BT_IODEV_H_
#define CRAS_BT_IODEV_H_

#include "cras_bt_device.h"

struct cras_iodev;

/* Creates a bluetooth iodev. */
struct cras_iodev *cras_bt_io_create(struct cras_iodev *dev,
				     enum cras_bt_device_profile profile);

/* Destroys a bluetooth iodev. */
void cras_bt_io_destroy(struct cras_iodev *bt_iodev);

#endif /* CRAS_BT_IODEV_H_ */
