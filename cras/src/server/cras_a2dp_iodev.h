/* Copyright (c) 2013 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef CRAS_BT_IODEV_H_
#define CRAS_BT_IODEV_H_

#include "cras_bt_transport.h"

/*
 * Creates an a2dp iodev from transport object.
 */
struct cras_iodev *a2dp_iodev_create(
		struct cras_bt_transport *transport);

/*
 * Destroys a2dp iodev.
 */
void a2dp_iodev_destroy(struct cras_iodev *iodev);

#endif /* CRS_BT_IODEV_H_ */
