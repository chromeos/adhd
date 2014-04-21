/* Copyright (c) 2013 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef CRAS_A2DP_IODEV_H_
#define CRAS_A2DP_IODEV_H_

#include "cras_bt_transport.h"

struct cras_iodev;

/* Callback to force suspend a a2dp iodev. */
typedef void (*a2dp_force_suspend_cb)(struct cras_iodev *iodev);

/*
 * Creates an a2dp iodev from transport object.
 * Args:
 *    transport - The transport to create a2dp iodev for
 *    force_suspend_cb - The callback to trigger when severe error occurs
 *        during transmitting audio, used to force suspend an a2dp iodev
 *        outside the life cycle controlled by bluetooth daemon.
 */
struct cras_iodev *a2dp_iodev_create(
		struct cras_bt_transport *transport,
		a2dp_force_suspend_cb force_suspend_cb);

/*
 * Destroys a2dp iodev.
 */
void a2dp_iodev_destroy(struct cras_iodev *iodev);

#endif /* CRS_A2DP_IODEV_H_ */
