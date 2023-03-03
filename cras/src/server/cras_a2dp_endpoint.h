/* Copyright 2013 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef CRAS_SRC_SERVER_CRAS_A2DP_ENDPOINT_H_
#define CRAS_SRC_SERVER_CRAS_A2DP_ENDPOINT_H_

#include <dbus/dbus.h>

struct cras_iodev;

int cras_a2dp_endpoint_create(DBusConnection* conn);

// Unregisters A2DP endpoint.
int cras_a2dp_endpoint_destroy(DBusConnection* conn);

// Gets the connected a2dp device, NULL is returned when there's none.
struct cras_bt_device* cras_a2dp_connected_device();

/* Suspends the connected a2dp device, the purpose is to remove a2dp iodev
 * to release a2dp audio before sending dbus message to disconnect a2dp
 * device. */
void cras_a2dp_suspend_connected_device(struct cras_bt_device* device);

// Starts A2DP output by creating the cras_iodev.
void cras_a2dp_start(struct cras_bt_device* device);

#endif  // CRAS_SRC_SERVER_CRAS_A2DP_ENDPOINT_H_
