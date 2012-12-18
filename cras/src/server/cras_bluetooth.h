/* Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef CRAS_BLUETOOTH_H_
#define CRAS_BLUETOOTH_H_

#include <dbus/dbus.h>

struct cras_bluetooth_device;

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
void cras_bluetooth_stop();

/* Returns a pointer to the first device attached to the adapter or NULL
 * if no devices are attached. Primarily intended for testing.
 */
const struct cras_bluetooth_device *cras_bluetooth_adapter_first_device();

/* Returns a pointer to the next device attached to the adapter or NULL
 * if no devices are attached. Primarily intended for testing.
 */
const struct cras_bluetooth_device *cras_bluetooth_adapter_next_device(
	const struct cras_bluetooth_device *device);

/* Return the D-Bus object path of the Bluetooth device. Primarily intended
 * for testing or logging.
 */
const char *cras_bluetooth_device_object_path(
	const struct cras_bluetooth_device *device);

#endif /* CRAS_BLUETOOTH_H_ */
