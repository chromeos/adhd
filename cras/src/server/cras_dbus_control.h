/* Copyright 2013 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef CRAS_SRC_SERVER_CRAS_DBUS_CONTROL_H_
#define CRAS_SRC_SERVER_CRAS_DBUS_CONTROL_H_

#include <dbus/dbus.h>
#include <stdbool.h>

#define CRAS_CONTROL_INTERFACE "org.chromium.cras.Control"
#define CRAS_ROOT_OBJECT_PATH "/org/chromium/cras"

// Starts the dbus control interface, begins listening for incoming messages.
void cras_dbus_control_start(DBusConnection* conn);

// Stops monitoring the dbus interface for command messages.
void cras_dbus_control_stop();

// Notify resourced that RTC is active.
void cras_dbus_notify_rtc_active(bool active);

#endif  // CRAS_SRC_SERVER_CRAS_DBUS_CONTROL_H_
