/* Copyright 2013 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef CRAS_SRC_SERVER_CRAS_BT_DEVICE_H_
#define CRAS_SRC_SERVER_CRAS_BT_DEVICE_H_

#include <dbus/dbus.h>
#include <stdbool.h>
#include <stdint.h>

#include "cras_types.h"

struct bt_io_manager;
struct cras_bt_adapter;
struct cras_bt_device;
struct cras_iodev;
struct cras_timer;

/* Object to represent a general bluetooth device, and used to
 * associate with some CRAS modules if it supports audio.
 */
struct cras_bt_device {
  // The dbus connection object used to send message to bluetoothd.
  DBusConnection* conn;
  // Object path of the bluetooth device.
  char* object_path;
  // The object path of the adapter associates with this device.
  char* adapter_obj_path;
  // The BT address of this device.
  char* address;
  // The readable name of this device.
  char* name;
  // The bluetooth class of this device.
  uint32_t bluetooth_class;
  // If this device is paired.
  int paired;
  // If this device is trusted.
  int trusted;
  // If this devices is connected.
  int connected;
  // OR'ed all connected audio profiles.
  unsigned int connected_profiles;
  // OR'ed by all audio profiles this device supports.
  unsigned int profiles;
  // OR'ed by all audio profiles this device actually
  // supports but is not scanned by BlueZ.
  unsigned int hidden_profiles;
  int use_hardware_volume;
  // The unique and persistent id of this bt_device.
  unsigned int stable_id;
  // The bt_io_manager in charge of managing iodevs of
  // different profiles and the switching in between.
  struct bt_io_manager* bt_io_mgr;

  struct cras_bt_device *prev, *next;
};

enum cras_bt_device_profile {
  CRAS_BT_DEVICE_PROFILE_A2DP_SOURCE = (1 << 0),
  CRAS_BT_DEVICE_PROFILE_A2DP_SINK = (1 << 1),
  CRAS_BT_DEVICE_PROFILE_AVRCP_REMOTE = (1 << 2),
  CRAS_BT_DEVICE_PROFILE_AVRCP_TARGET = (1 << 3),
  CRAS_BT_DEVICE_PROFILE_HFP_HANDSFREE = (1 << 4),
  CRAS_BT_DEVICE_PROFILE_HFP_AUDIOGATEWAY = (1 << 5)
};

static const unsigned int CRAS_SUPPORTED_PROFILES =
    CRAS_BT_DEVICE_PROFILE_A2DP_SINK | CRAS_BT_DEVICE_PROFILE_HFP_HANDSFREE;

enum cras_bt_device_profile cras_bt_device_profile_from_uuid(const char* uuid);

struct cras_bt_device* cras_bt_device_create(DBusConnection* conn,
                                             const char* object_path);

/*
 * Removes a BT device from record. If this device is connected state,
 * ensure the associated A2DP and HFP AG be removed cleanly.
 */
void cras_bt_device_remove(struct cras_bt_device* device);

void cras_bt_device_reset();

struct cras_bt_device* cras_bt_device_get(const char* object_path);

/* Checks if the target bt device is still valid. Used in async events
 * from audio thread to main thread where bt device could have already
 * been destroyed. */
bool cras_bt_device_valid(const struct cras_bt_device* target);

const char* cras_bt_device_object_path(const struct cras_bt_device* device);

// Gets the stable id of given cras_bt_device.
int cras_bt_device_get_stable_id(const struct cras_bt_device* device);

struct cras_bt_adapter* cras_bt_device_adapter(
    const struct cras_bt_device* device);
const char* cras_bt_device_address(const struct cras_bt_device* device);
const char* cras_bt_device_name(const struct cras_bt_device* device);
int cras_bt_device_paired(const struct cras_bt_device* device);
int cras_bt_device_trusted(const struct cras_bt_device* device);
int cras_bt_device_connected(const struct cras_bt_device* device);

void cras_bt_device_update_properties(struct cras_bt_device* device,
                                      DBusMessageIter* properties_array_iter,
                                      DBusMessageIter* invalidated_array_iter);

static inline int cras_bt_device_is_profile_connected(
    const struct cras_bt_device* device,
    enum cras_bt_device_profile profile) {
  return !!(device->connected_profiles & profile);
}

// Updates the supported profiles on dev. Expose for unit test.
int cras_bt_device_set_supported_profiles(struct cras_bt_device* device,
                                          unsigned int profiles);

// Checks if profile is claimed supported by the device.
static inline int cras_bt_device_supports_profile(
    const struct cras_bt_device* device,
    enum cras_bt_device_profile profile) {
  return !!(device->profiles & profile);
}

/* Sets if the BT audio device should use hardware volume.
 * Args:
 *    device - The remote bluetooth audio device.
 *    use_hardware_volume - Set to true to indicate hardware volume
 *        is preferred over software volume.
 */
void cras_bt_device_set_use_hardware_volume(struct cras_bt_device* device,
                                            int use_hardware_volume);

// Gets if the BT audio device should use hardware volume.
int cras_bt_device_get_use_hardware_volume(struct cras_bt_device* device);

/* Forces disconnect the bt device. Used when handling audio error
 * that we want to make the device be completely disconnected from
 * host to reflect the state that an error has occurred.
 * Args:
 *    conn - The dbus connection.
 *    device - The bt device to disconnect.
 */
int cras_bt_device_disconnect(DBusConnection* conn,
                              struct cras_bt_device* device);

/* Gets the SCO socket for the device.
 * Args:
 *     device - The device object to get SCO socket for.
 *     codec - 1 for CVSD, 2 for mSBC
 *     use_offload - True for using offloading path; false otherwise.
 */
int cras_bt_device_sco_connect(struct cras_bt_device* device,
                               int codec,
                               bool use_offload);

/* Gets the SCO packet size in bytes, used by HFP iodev for audio I/O.
 * The logic is built base on experience: for USB bus, respect BT Core spec
 * that has clear recommendation of packet size of codecs (CVSD, mSBC).
 * As for other buses, use the MTU value of SCO socket filled by driver.
 * Args:
 *    device - The bt device to query mtu.
 *    sco_socket - The SCO socket.
 *    codec - 1 for CVSD, 2 for mSBC per HFP 1.7 specification.
 */
int cras_bt_device_sco_packet_size(struct cras_bt_device* device,
                                   int sco_socket,
                                   int codec);

/* Appends an iodev to bt device.
 * Args:
 *    device - The device to append iodev to.
 *    iodev - The iodev to add.
 *    btflag - The profile of the iodev about to add.
 */
void cras_bt_device_append_iodev(struct cras_bt_device* device,
                                 struct cras_iodev* iodev,
                                 enum CRAS_BT_FLAGS btflag);

/* Removes an iodev from bt device.
 * Args:
 *    device - The device to remove iodev from.
 *    iodev - The iodev to remove.
 */
void cras_bt_device_rm_iodev(struct cras_bt_device* device,
                             struct cras_iodev* iodev);

// Checks if the device has an iodev for A2DP.
int cras_bt_device_has_a2dp(struct cras_bt_device* device);

// Updates the volume to bt_device when a volume change event is reported.
void cras_bt_device_update_hardware_volume(struct cras_bt_device* device,
                                           int volume);

// Notifies bt_device that a2dp connection is configured.
void cras_bt_device_a2dp_configured(struct cras_bt_device* device);

//
void cras_bt_device_remove_conflict(struct cras_bt_device* device);

//
int cras_bt_device_connect_profile(DBusConnection* conn,
                                   struct cras_bt_device* device,
                                   const char* uuid);

/* Notifies bt device that audio gateway is initialized.
 * Args:
 *   device - The bluetooth device.
 * Returns:
 *   0 on success, error code otherwise.
 */
int cras_bt_device_audio_gateway_initialized(struct cras_bt_device* device);

/*
 * Notifies bt device about a profile no longer works. It could be caused
 * by initialize failure or fatal error has occurred.
 * Args:
 *    device - The bluetooth audio device.
 *    profile - The BT audio profile that has dropped.
 */
void cras_bt_device_notify_profile_dropped(struct cras_bt_device* device,
                                           enum cras_bt_device_profile profile);

/*
 * Establishes SCO connection if it has not been established on the BT device.
 * Note: this function should be only used for hfp_alsa_io.
 * Args:
 *    device - The bluetooth device.
 *    codec - 1 for CVSD, 2 for mSBC
 * Returns:
 *   0 on success, error code otherwise.
 */
int cras_bt_device_get_sco(struct cras_bt_device* device, int codec);

/*
 * Closes SCO connection if the caller is the last user for the connection on
 * the BT device.
 * Note: this function should be only used for hfp_alsa_io.
 * Args:
 *   device - The bluetooth device.
 */
void cras_bt_device_put_sco(struct cras_bt_device* device);

/* Gets the SCO HCI handle, only used for KPI metrics.
 * Args:
 *   sco_socket - The SCO socket.
 * Returns:
 *   The handle on success, -1 otherwise
 */
int cras_bt_device_sco_handle(int sco_socket);

/*
 * Report HFP start/stop event to BlueZ. This is only needed for KPI metrics.
 * Args:
 *   device - The bluetooth device.
 *   status - True for start, false for stop.
 *   sco_handle - The SCO HCI handle. Applicable only when status = true.
 * Returns:
 *   0 on success, error code otherwise.
 */
int cras_bt_device_report_hfp_start_stop_status(struct cras_bt_device* device,
                                                bool status,
                                                int sco_handle);

/*
 * Restart HFP, only used for handling POLLHUP in an existing connection.
 * Args:
 *   device - The bluetooth device.
 */
void cras_bt_device_hfp_reconnect(struct cras_bt_device* device);
#endif  // CRAS_SRC_SERVER_CRAS_BT_DEVICE_H_
