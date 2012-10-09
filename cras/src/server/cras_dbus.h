/* Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef CRAS_DBUS_H_
#define CRAS_DBUS_H_

#include <dbus/dbus.h>

void cras_dbus_connect_system_bus();
DBusConnection *cras_dbus_system_bus();
void cras_dbus_disconnect_system_bus();

#endif /* CRAS_DBUS_H_ */
