/* Copyright (c) 2013 The Chromium Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef CRAS_A2DP_ENDPOINT_H_
#define CRAS_A2DP_ENDPOINT_H_

#include <dbus/dbus.h>

int cras_a2dp_endpoint_create(DBusConnection *conn);

/* Gets the connected a2dp device, NULL is returned when there's none. */
struct cras_bt_device *cras_a2dp_connected_device();

#endif /* CRAS_A2DP_ENDPOINT_H_ */
