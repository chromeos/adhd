/* Copyright (c) 2013 The Chromium Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef CRAS_A2DP_ENDPOINT_H_
#define CRAS_A2DP_ENDPOINT_H_

#include <dbus/dbus.h>

struct cras_iodev;

int cras_a2dp_endpoint_create(DBusConnection *conn);

/* Gets the connected a2dp device, NULL is returned when there's none. */
struct cras_bt_device *cras_a2dp_connected_device();

/* Suspends the connected a2dp device, the purpose is to remove a2dp iodev
 * to release a2dp audio before sending dbus message to disconnect a2dp
 * device. */
void cras_a2dp_suspend_connected_device();

/* Checks if a suspend timer has been scheduled. */
int cras_a2dp_has_suspend_timer();

/* Schedules a suspend timer after certain period of time.
 * Args:
 *    iodev - The a2dp iodev to suspend.
 *    msec - The timeout in millisecond.
 */
void cras_a2dp_schedule_suspend_timer(struct cras_iodev *iodev,
				      unsigned int msec);

/* Cancels any suspend timer scheduled for iodev.
 * Args:
 *    iodev - The a2dp iodev to cancel suspend for.
 */
void cras_a2dp_cancel_suspend_timer(struct cras_iodev *iodev);

#endif /* CRAS_A2DP_ENDPOINT_H_ */
