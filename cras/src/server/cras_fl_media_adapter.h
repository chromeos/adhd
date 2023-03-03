/* Copyright 2022 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef CRAS_SRC_SERVER_CRAS_FL_MEDIA_ADAPTER_H_
#define CRAS_SRC_SERVER_CRAS_FL_MEDIA_ADAPTER_H_

#include <stdint.h>

#include "cras/src/server/cras_a2dp_manager.h"
#include "cras/src/server/cras_bt_io.h"
#include "cras/src/server/cras_fl_manager.h"
#include "cras/src/server/cras_hfp_manager.h"

#define BT_MEDIA_OBJECT_PATH_SIZE_MAX 128

/* Hold information and focus on logic related to communicate with the
 * Bluetooth stack through DBus. Information and logic regarding A2DP and
 * AVRCP should be kept in the cras_a2dp for responsibility division.
 */
struct fl_media {
  // The id of HCI interface to use.
  unsigned int hci;
  // Object path of the Bluetooth media.
  char obj_path[BT_MEDIA_OBJECT_PATH_SIZE_MAX];
  // The DBus connection object used to send message to Floss Media
  // interface.
  DBusConnection* conn;
  // Object representing the connected A2DP headset.
  struct cras_a2dp* a2dp;
  // Object representing the connected HFP headset.
  struct cras_hfp* hfp;
  struct bt_io_manager* bt_io_mgr;
};

/* Sets up new a2dp and hfp and feeds them into active_fm when bluetooth
 * device is added.
 *
 * Args:
 *   active_fm - The active fl_media struct used for interacting with the
 *               Floss interface.
 *   addr - The address of the added bluetooth device.
 *   name - The remote name of the added bluetooth device.
 *   codecs - Linked list of supported a2dp codecs. NULL for A2DP not supported.
 *   hfp_cap - A bitmask of HFP capability defined by Floss.
 *   abs_vol_supported - Absolute volume supported by the bluetooth device.
 * Returns:
 *   int - Returns a negative errno if an error occurs, 0 otherwise.
 */
int handle_on_bluetooth_device_added(struct fl_media* active_fm,
                                     const char* addr,
                                     const char* name,
                                     struct cras_fl_a2dp_codec_config* codecs,
                                     int32_t hfp_cap,
                                     bool abs_vol_supported);

/* Suspends a2dp and hfp if existed when bluetooth device is removed.
 *
 * Args:
 *   active_fm - The active fl_media struct used for interacting with the
 *               Floss interface.
 *   addr - The address of the added bluetooth device.
 * Returns:
 *   int - Returns a negative errno if an error occurs, 0 otherwise.
 */
int handle_on_bluetooth_device_removed(struct fl_media* active_fm,
                                       const char* addr);

/* Sets supported absolute volume on floss a2dp.
 *
 * Args:
 *   active_fm - The active fl_media struct used for interacting with the
 *               Floss interface.
 *   abs_vol_supported - Absolute volume supported by the bluetooth device.
 * Returns:
 *   int - Returns a negative errno if an error occurs, 0 otherwise.
 */
int handle_on_absolute_volume_supported_changed(struct fl_media* active_fm,
                                                bool abs_vol_supported);

/* Updates a2dp volume to bt_io_manager.
 *
 * Args:
 *   active_fm - The active fl_media struct used for interacting with the
 *               Floss interface.
 *   volume - Hardware volume of the bluetooth device.
 * Returns:
 *   int - Returns a negative errno if an error occurs, 0 otherwise.
 */
int handle_on_absolute_volume_changed(struct fl_media* active_fm,
                                      uint8_t volume);

/* Updates hfp volume to bt_io_manager.
 *
 * Args:
 *   active_fm - The active fl_media struct used for interacting with the
 *               Floss interface.
 *   addr - The address of the added bluetooth device.
 *   volume - Hardware volume of the bluetooth device.
 * Returns:
 *   int - Returns a negative errno if an error occurs, 0 otherwise.
 */
int handle_on_hfp_volume_changed(struct fl_media* active_fm,
                                 const char* addr,
                                 uint8_t volume);

/* Checks if it is the headset that issued disconnection.
 * If true, restarts the iodev as an attempt of reconnection.
 *
 * Args:
 *   active_fm - The active fl_media struct used for interacting with the
 *               Floss interface.
 *   addr - The address of the added bluetooth device.
 * Returns:
 *   int - Returns a negative errno if an error occurs, 0 otherwise.
 */
int handle_on_hfp_audio_disconnected(struct fl_media* active_fm,
                                     const char* addr);

/* Destroys struct fl_media and frees up relevant resources.
 *
 * Args:
 *   active_fm - The fl_media struct to be destroyed.
 */
void fl_media_destroy(struct fl_media** active_fm);
#endif  // CRAS_FL_MEDIA_ADAPTOR_H_
