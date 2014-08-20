/* Copyright (c) 2014 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef CRAS_BT_IODEV_H_
#define CRAS_BT_IODEV_H_

#include "cras_bt_device.h"

struct cras_iodev;

/* Creates a bluetooth iodev. */
struct cras_iodev *cras_bt_io_create(struct cras_bt_device *device,
				     struct cras_iodev *dev,
				     enum cras_bt_device_profile profile);

/* Destroys a bluetooth iodev. */
void cras_bt_io_destroy(struct cras_iodev *bt_iodev);

/* Checks if dev is attached to the bt_iodev. */
int cras_bt_io_has_dev(struct cras_iodev *bt_iodev, struct cras_iodev *dev);

/* Appends a profile specific iodev to bt_iodev. */
int cras_bt_io_append(struct cras_iodev *bt_iodev,
		      struct cras_iodev *dev,
		      enum cras_bt_device_profile profile);

/* Removes a profile specific iodev from bt_iodev. */
int cras_bt_io_remove(struct cras_iodev *bt_iodev, struct cras_iodev *dev);

#endif /* CRAS_BT_IODEV_H_ */
