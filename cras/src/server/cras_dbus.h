/* Copyright 2012 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef CRAS_SRC_SERVER_CRAS_DBUS_H_
#define CRAS_SRC_SERVER_CRAS_DBUS_H_

#include <dbus/dbus.h>

// Establish connection to the D-Bus System Bus.
DBusConnection* cras_dbus_connect_system_bus();

/* Dispatch pending incoming and outgoing messages.
 *
 * This function must be called from the main loop to dispatch any
 * pending incoming and outgoing messages to the appropriate registered
 * object handler functions or filter functions - including those internal
 * to libdbus.
 *
 * It does nothing if there are no pending messages.
 */
void cras_dbus_dispatch(DBusConnection* conn);

// Disconnect from the D-Bus System Bus.
void cras_dbus_disconnect_system_bus(DBusConnection* conn);

#endif  // CRAS_SRC_SERVER_CRAS_DBUS_H_
