/* Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef CRAS_BLUETOOTH_H_
#define CRAS_BLUETOOTH_H_

typedef struct DBusConnection DBusConnection;

/* Return the D-Bus object path of the Bluetooth adapter. Primarily intended
 * for testing or logging.
 */
const char *cras_bluetooth_adapter_object_path();

/* Communicate with the system bluetooth daemon to obtain and monitor changes
 * with the default bluetooth adapter and connected devices.
 */
void cras_bluetooth_start(DBusConnection *conn);

/* Cease monitoring the bluetooth daemon and clear information about the
 * default bluetooth adapter and connected devices.
 */
void cras_bluetooth_stop(DBusConnection *conn);


#endif /* CRAS_BLUETOOTH_H_ */
