/* Copyright (c) 2013 The Chromium Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef CRAS_BT_DEVICE_H_
#define CRAS_BT_DEVICE_H_

#include <dbus/dbus.h>

struct cras_bt_adapter;
struct cras_bt_device;
struct cras_iodev;

enum cras_bt_device_profile {
	CRAS_BT_DEVICE_PROFILE_A2DP_SOURCE	= (1 << 0),
	CRAS_BT_DEVICE_PROFILE_A2DP_SINK	= (1 << 1),
	CRAS_BT_DEVICE_PROFILE_AVRCP_REMOTE	= (1 << 2),
	CRAS_BT_DEVICE_PROFILE_AVRCP_TARGET	= (1 << 3),
	CRAS_BT_DEVICE_PROFILE_HFP_HANDSFREE	= (1 << 4),
	CRAS_BT_DEVICE_PROFILE_HFP_AUDIOGATEWAY	= (1 << 5),
	CRAS_BT_DEVICE_PROFILE_HSP_HEADSET	= (1 << 6),
	CRAS_BT_DEVICE_PROFILE_HSP_AUDIOGATEWAY = (1 << 7)
};

enum cras_bt_device_profile cras_bt_device_profile_from_uuid(const char *uuid);

struct cras_bt_device *cras_bt_device_create(const char *object_path);
void cras_bt_device_destroy(struct cras_bt_device *device);
void cras_bt_device_reset();

struct cras_bt_device *cras_bt_device_get(const char *object_path);
size_t cras_bt_device_get_list(struct cras_bt_device ***device_list_out);

const char *cras_bt_device_object_path(const struct cras_bt_device *device);
struct cras_bt_adapter *cras_bt_device_adapter(
	const struct cras_bt_device *device);
const char *cras_bt_device_address(const struct cras_bt_device *device);
const char *cras_bt_device_name(const struct cras_bt_device *device);
int cras_bt_device_paired(const struct cras_bt_device *device);
int cras_bt_device_trusted(const struct cras_bt_device *device);
int cras_bt_device_connected(const struct cras_bt_device *device);

void cras_bt_device_update_properties(struct cras_bt_device *device,
				      DBusMessageIter *properties_array_iter,
				      DBusMessageIter *invalidated_array_iter);

/* Gets the SCO socket for the device.
 * Args:
 *     device - The device object to get SCO socket for.
 */
int cras_bt_device_sco_connect(struct cras_bt_device *device);

/* Queries the preffered mtu value for SCO socket. */
int cras_bt_device_sco_mtu(struct cras_bt_device *device, int sco_socket);

/* Sets the speaker gain for bt device, note this is for HFP/HSP mode.
 * Args:
 *    device - The device object to set speaker gain.
 *    gain - value between 0-100.
 */
int cras_bt_device_set_speaker_gain(struct cras_bt_device *device, int gain);

/* Appends an iodev to bt device.
 * Args:
 *    device - The device to append iodev to.
 *    iodev - The iodev to add.
 *    profile - The profile of the iodev about to add.
 */
void cras_bt_device_append_iodev(struct cras_bt_device *device,
				 struct cras_iodev *iodev,
				 enum cras_bt_device_profile profile);

/* Removes an iodev from bt device.
 * Args:
 *    device - The device to remove iodev from.
 *    iodev - The iodev to remove.
 */
void cras_bt_device_rm_iodev(struct cras_bt_device *device,
			     struct cras_iodev *iodev);

/* Gets the active profile of the bt device. */
int cras_bt_device_get_active_profile(const struct cras_bt_device *device);

/* Sets the active profile of the bt device. */
void cras_bt_device_set_active_profile(struct cras_bt_device *device,
				       unsigned int profile);

/* Calls this function after configures the active profile of bt device
 * will reactivate the bt_ios associated with the bluetooth device. So it
 * can switch to the new active profile at open.
 * Args:
 *    device - The bluetooth device.
 *    bt_iodev - The iodev triggers the reactivaion.
 */
int cras_bt_device_switch_profile_on_open(struct cras_bt_device *device,
					  struct cras_iodev *bt_iodev);

/* Calls this function after configures the active profile of bt device
 * will reactivate the bt_ios associated with the bluetooth device. So it
 * can switch to the new active profile at device close.
 * Args:
 *    device - The bluetooth device.
 *    bt_iodev - The iodev triggers the reactivaion.
 */
int cras_bt_device_switch_profile_on_close(struct cras_bt_device *device,
					   struct cras_iodev *bt_iodev);

void cras_bt_device_start_monitor();

/* Checks if the device has an iodev for A2DP. */
int cras_bt_device_has_a2dp(struct cras_bt_device *device);

/* Returns true if and only if device has an iodev for A2DP and the bt device
 * is not opening for audio capture.
 */
int cras_bt_device_can_switch_to_a2dp(struct cras_bt_device *device);

#endif /* CRAS_BT_DEVICE_H_ */
