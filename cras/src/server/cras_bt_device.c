/* Copyright 2013 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef _GNU_SOURCE
#define _GNU_SOURCE  // for ppoll
#endif

#include "cras/src/server/cras_bt_device.h"

#include <dbus/dbus.h>
#include <errno.h>
#include <poll.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <syslog.h>

#include "cras/src/common/bluetooth.h"
#include "cras/src/common/cras_string.h"
#include "cras/src/server/cras_a2dp_endpoint.h"
#include "cras/src/server/cras_bt_adapter.h"
#include "cras/src/server/cras_bt_constants.h"
#include "cras/src/server/cras_bt_io.h"
#include "cras/src/server/cras_bt_log.h"
#include "cras/src/server/cras_bt_policy.h"
#include "cras/src/server/cras_bt_profile.h"
#include "cras/src/server/cras_hfp_ag_profile.h"
#include "cras/src/server/cras_hfp_slc.h"
#include "cras/src/server/cras_iodev.h"
#include "cras/src/server/cras_iodev_list.h"
#include "cras/src/server/cras_server_metrics.h"
#include "third_party/superfasthash/sfh.h"
#include "third_party/utlist/utlist.h"

/*
 * Bluetooth Core 5.0 spec, vol 4, part B, section 2 describes
 * the recommended HCI packet size in one USB transfer for CVSD
 * and MSBC codec.
 */
#define USB_MSBC_PKT_SIZE 60
#define USB_CVSD_PKT_SIZE 48
#define DEFAULT_SCO_PKT_SIZE USB_CVSD_PKT_SIZE

static const unsigned int PROFILE_DROP_SUSPEND_DELAY_MS = 5000;

/* This is used when a critical SCO failure happens and is worth scheduling a
 * suspend in case for some reason BT headset stays connected in baseband and
 * confuses user.
 */
static const unsigned int SCO_SUSPEND_DELAY_MS = 5000;

static struct cras_bt_device* devices;

enum cras_bt_device_profile cras_bt_device_profile_from_uuid(const char* uuid) {
  if (strcmp(uuid, HFP_HF_UUID) == 0) {
    return CRAS_BT_DEVICE_PROFILE_HFP_HANDSFREE;
  } else if (strcmp(uuid, HFP_AG_UUID) == 0) {
    return CRAS_BT_DEVICE_PROFILE_HFP_AUDIOGATEWAY;
  } else if (strcmp(uuid, A2DP_SOURCE_UUID) == 0) {
    return CRAS_BT_DEVICE_PROFILE_A2DP_SOURCE;
  } else if (strcmp(uuid, A2DP_SINK_UUID) == 0) {
    return CRAS_BT_DEVICE_PROFILE_A2DP_SINK;
  } else if (strcmp(uuid, AVRCP_REMOTE_UUID) == 0) {
    return CRAS_BT_DEVICE_PROFILE_AVRCP_REMOTE;
  } else if (strcmp(uuid, AVRCP_TARGET_UUID) == 0) {
    return CRAS_BT_DEVICE_PROFILE_AVRCP_TARGET;
  } else {
    return 0;
  }
}

struct cras_bt_device* cras_bt_device_create(DBusConnection* conn,
                                             const char* object_path) {
  struct cras_bt_device* device;

  device = calloc(1, sizeof(*device));
  if (device == NULL) {
    return NULL;
  }

  device->bt_io_mgr = bt_io_manager_create();
  if (device->bt_io_mgr == NULL) {
    free(device);
    return NULL;
  }

  device->conn = conn;
  device->object_path = strdup(object_path);
  if (device->object_path == NULL) {
    free(device);
    return NULL;
  }
  device->stable_id =
      SuperFastHash(device->object_path, strlen(device->object_path),
                    strlen(device->object_path));

  DL_APPEND(devices, device);

  return device;
}

static void on_connect_profile_reply(DBusPendingCall* pending_call,
                                     void* data) {
  DBusMessage* reply;

  reply = dbus_pending_call_steal_reply(pending_call);
  dbus_pending_call_unref(pending_call);

  if (dbus_message_get_type(reply) == DBUS_MESSAGE_TYPE_ERROR) {
    syslog(LOG_WARNING, "Connect profile message replied error: %s",
           dbus_message_get_error_name(reply));
  }

  dbus_message_unref(reply);
}

static void on_disconnect_reply(DBusPendingCall* pending_call, void* data) {
  DBusMessage* reply;

  reply = dbus_pending_call_steal_reply(pending_call);
  dbus_pending_call_unref(pending_call);

  if (dbus_message_get_type(reply) == DBUS_MESSAGE_TYPE_ERROR) {
    syslog(LOG_WARNING, "Disconnect message replied error");
  }

  dbus_message_unref(reply);
}

int cras_bt_device_connect_profile(DBusConnection* conn,
                                   struct cras_bt_device* device,
                                   const char* uuid) {
  DBusMessage* method_call;
  DBusError dbus_error;
  DBusPendingCall* pending_call;

  method_call =
      dbus_message_new_method_call(BLUEZ_SERVICE, device->object_path,
                                   BLUEZ_INTERFACE_DEVICE, "ConnectProfile");
  if (!method_call) {
    return -ENOMEM;
  }

  if (!dbus_message_append_args(method_call, DBUS_TYPE_STRING, &uuid,
                                DBUS_TYPE_INVALID)) {
    dbus_message_unref(method_call);
    return -ENOMEM;
  }

  dbus_error_init(&dbus_error);

  pending_call = NULL;
  if (!dbus_connection_send_with_reply(conn, method_call, &pending_call,
                                       DBUS_TIMEOUT_USE_DEFAULT)) {
    dbus_message_unref(method_call);
    syslog(LOG_WARNING, "Failed to send Disconnect message");
    return -EIO;
  }

  dbus_message_unref(method_call);
  if (!dbus_pending_call_set_notify(pending_call, on_connect_profile_reply,
                                    conn, NULL)) {
    dbus_pending_call_cancel(pending_call);
    dbus_pending_call_unref(pending_call);
    return -EIO;
  }
  return 0;
}

int cras_bt_device_disconnect(DBusConnection* conn,
                              struct cras_bt_device* device) {
  DBusMessage* method_call;
  DBusError dbus_error;
  DBusPendingCall* pending_call;

  method_call = dbus_message_new_method_call(
      BLUEZ_SERVICE, device->object_path, BLUEZ_INTERFACE_DEVICE, "Disconnect");
  if (!method_call) {
    return -ENOMEM;
  }

  dbus_error_init(&dbus_error);

  pending_call = NULL;
  if (!dbus_connection_send_with_reply(conn, method_call, &pending_call,
                                       DBUS_TIMEOUT_USE_DEFAULT)) {
    dbus_message_unref(method_call);
    syslog(LOG_WARNING, "Failed to send Disconnect message");
    return -EIO;
  }

  dbus_message_unref(method_call);
  if (!dbus_pending_call_set_notify(pending_call, on_disconnect_reply, conn,
                                    NULL)) {
    dbus_pending_call_cancel(pending_call);
    dbus_pending_call_unref(pending_call);
    return -EIO;
  }
  return 0;
}

static void cras_bt_device_destroy(struct cras_bt_device* device) {
  DL_DELETE(devices, device);

  cras_bt_policy_remove_device(device);

  bt_io_manager_destroy(device->bt_io_mgr);
  free(device->adapter_obj_path);
  free(device->object_path);
  free(device->address);
  free(device->name);
  free(device);
}

void cras_bt_device_remove(struct cras_bt_device* device) {
  /*
   * We expect BT stack to disconnect this device before removing it,
   * but it may not the case if there's issue at BT side. Print error
   * log whenever this happens.
   */
  if (device->connected) {
    syslog(LOG_WARNING, "Removing dev with connected profiles %u",
           device->connected_profiles);
  }
  /*
   * Possibly clean up the associated A2DP and HFP AG iodevs that are
   * still accessing this device.
   */
  cras_a2dp_suspend_connected_device(device);
  cras_hfp_ag_suspend_connected_device(device);
  cras_bt_device_destroy(device);
}

void cras_bt_device_reset() {
  while (devices) {
    syslog(LOG_INFO, "Bluetooth Device: %s removed", devices->address);
    cras_bt_device_destroy(devices);
  }
}

struct cras_bt_device* cras_bt_device_get(const char* object_path) {
  struct cras_bt_device* device;

  DL_FOREACH (devices, device) {
    if (strcmp(device->object_path, object_path) == 0) {
      return device;
    }
  }

  return NULL;
}

bool cras_bt_device_valid(const struct cras_bt_device* target) {
  struct cras_bt_device* device;

  DL_FOREACH (devices, device) {
    if (device == target) {
      return true;
    }
  }
  return false;
}

const char* cras_bt_device_object_path(const struct cras_bt_device* device) {
  return device->object_path;
}

int cras_bt_device_get_stable_id(const struct cras_bt_device* device) {
  return device->stable_id;
}

struct cras_bt_adapter* cras_bt_device_adapter(
    const struct cras_bt_device* device) {
  return cras_bt_adapter_get(device->adapter_obj_path);
}

const char* cras_bt_device_address(const struct cras_bt_device* device) {
  return device->address;
}

const char* cras_bt_device_name(const struct cras_bt_device* device) {
  return device->name;
}

void cras_bt_device_append_iodev(struct cras_bt_device* device,
                                 struct cras_iodev* iodev,
                                 enum CRAS_BT_FLAGS btflag) {
  /*
   * We only support software gain scalar for input device, so it doesn't
   * matter if the software_volume_needed for input.
   */
  if (iodev->direction == CRAS_STREAM_OUTPUT) {
    iodev->software_volume_needed = !device->use_hardware_volume;
  }

  bt_io_manager_append_iodev(device->bt_io_mgr, iodev, btflag);
  /*
   * BlueZ doesn't guarantee the call sequence and
   * cras_bt_device_set_use_hardware_volume may have been called.
   */
  bt_io_manager_set_use_hardware_volume(device->bt_io_mgr,
                                        device->use_hardware_volume);
}

void cras_bt_device_rm_iodev(struct cras_bt_device* device,
                             struct cras_iodev* iodev) {
  bt_io_manager_remove_iodev(device->bt_io_mgr, iodev);
}

void cras_bt_device_a2dp_configured(struct cras_bt_device* device) {
  BTLOG(btlog, BT_A2DP_CONFIGURED, device->connected_profiles, 0);
  device->connected_profiles |= CRAS_BT_DEVICE_PROFILE_A2DP_SINK;
}

int cras_bt_device_has_a2dp(struct cras_bt_device* device) {
  return bt_io_manager_has_a2dp(device->bt_io_mgr);
}

void cras_bt_device_remove_conflict(struct cras_bt_device* device) {
  struct cras_bt_device* connected;

  // Suspend other HFP audio gateways that conflict with device.
  cras_hfp_ag_remove_conflict(device);

  // Check if there's conflict A2DP headset and suspend it.
  connected = cras_a2dp_connected_device();
  if (connected && (connected != device)) {
    cras_a2dp_suspend_connected_device(connected);
  }
}

int cras_bt_device_audio_gateway_initialized(struct cras_bt_device* device) {
  BTLOG(btlog, BT_AUDIO_GATEWAY_INIT, device->profiles, 0);
  /* Marks HFP as connected. This is what connection watcher
   * checks. */
  device->connected_profiles |= CRAS_BT_DEVICE_PROFILE_HFP_HANDSFREE;

  /* If device connects HFP but not reporting correct UUID, manually add
   * it to allow CRAS to enumerate audio node for it. We're seeing this
   * behavior on qualification test software. */
  if (!cras_bt_device_supports_profile(device,
                                       CRAS_BT_DEVICE_PROFILE_HFP_HANDSFREE)) {
    unsigned int profiles =
        device->profiles | CRAS_BT_DEVICE_PROFILE_HFP_HANDSFREE;
    cras_bt_device_set_supported_profiles(device, profiles);
    device->hidden_profiles |= CRAS_BT_DEVICE_PROFILE_HFP_HANDSFREE;
    cras_bt_policy_start_connection_watch(device);
  }

  return 0;
}

static void cras_bt_device_log_profile(const struct cras_bt_device* device,
                                       enum cras_bt_device_profile profile) {
  switch (profile) {
    case CRAS_BT_DEVICE_PROFILE_HFP_HANDSFREE:
      syslog(LOG_DEBUG, "Bluetooth Device: %s is HFP handsfree",
             device->address);
      break;
    case CRAS_BT_DEVICE_PROFILE_HFP_AUDIOGATEWAY:
      syslog(LOG_DEBUG, "Bluetooth Device: %s is HFP audio gateway",
             device->address);
      break;
    case CRAS_BT_DEVICE_PROFILE_A2DP_SOURCE:
      syslog(LOG_DEBUG, "Bluetooth Device: %s is A2DP source", device->address);
      break;
    case CRAS_BT_DEVICE_PROFILE_A2DP_SINK:
      syslog(LOG_DEBUG, "Bluetooth Device: %s is A2DP sink", device->address);
      break;
    case CRAS_BT_DEVICE_PROFILE_AVRCP_REMOTE:
      syslog(LOG_DEBUG, "Bluetooth Device: %s is AVRCP remote",
             device->address);
      break;
    case CRAS_BT_DEVICE_PROFILE_AVRCP_TARGET:
      syslog(LOG_DEBUG, "Bluetooth Device: %s is AVRCP target",
             device->address);
      break;
  }
}

static void cras_bt_device_log_profiles(const struct cras_bt_device* device,
                                        unsigned int profiles) {
  unsigned int profile;

  while (profiles) {
    // Get the LSB of profiles
    profile = profiles & -profiles;
    cras_bt_device_log_profile(device, profile);
    profiles ^= profile;
  }
}

static void cras_bt_device_set_connected(struct cras_bt_device* device,
                                         int value) {
  if (!device->connected && value) {
    BTLOG(btlog, BT_DEV_CONNECTED, device->profiles, device->stable_id);
  }

  if (device->connected && !value) {
    BTLOG(btlog, BT_DEV_DISCONNECTED, device->profiles, device->stable_id);
    cras_bt_profile_on_device_disconnected(device);
    /* Device is disconnected, resets connected profiles and the
     * suspend timer which scheduled earlier. */
    device->connected_profiles = 0;
    cras_bt_policy_cancel_suspend(device);
  }

  device->connected = value;

  if (!device->connected) {
    cras_bt_policy_stop_connection_watch(device);
  }
}

void cras_bt_device_notify_profile_dropped(
    struct cras_bt_device* device,
    enum cras_bt_device_profile profile) {
  device->connected_profiles &= ~profile;

  // Do nothing if device already disconnected.
  if (!device->connected) {
    return;
  }

  /* If any profile, a2dp or hfp/hsp, has dropped for some reason,
   * we shall make sure this device is fully disconnected within
   * given time so that user does not see a headset stay connected
   * but works with partial function.
   */
  cras_bt_policy_schedule_suspend(device, PROFILE_DROP_SUSPEND_DELAY_MS,
                                  UNEXPECTED_PROFILE_DROP);
}

/* Refresh the list of known supported profiles.
 * Args:
 *    device - The BT device holding scanned profiles bitmap.
 *    profiles - The OR'ed profiles the device claims to support as is notified
 *               by BlueZ.
 * Returns:
 *    The OR'ed profiles that are both supported by Cras and isn't previously
 *    supported by the device.
 */
int cras_bt_device_set_supported_profiles(struct cras_bt_device* device,
                                          unsigned int profiles) {
  // Do nothing if no new profiles.
  if ((device->profiles & profiles) == profiles) {
    return 0;
  }

  unsigned int new_profiles = profiles & ~device->profiles;

  /* Log this event as we might need to re-initialize the BT audio nodes
   * if new audio profile is reported for already connected device. */
  if (device->connected && (new_profiles & CRAS_SUPPORTED_PROFILES)) {
    BTLOG(btlog, BT_NEW_AUDIO_PROFILE_AFTER_CONNECT, device->profiles,
          new_profiles);
  }
  cras_bt_device_log_profiles(device, new_profiles);
  device->profiles = profiles | device->hidden_profiles;

  return (new_profiles & CRAS_SUPPORTED_PROFILES);
}

void cras_bt_device_update_properties(struct cras_bt_device* device,
                                      DBusMessageIter* properties_array_iter,
                                      DBusMessageIter* invalidated_array_iter) {
  int watch_needed = 0;
  while (dbus_message_iter_get_arg_type(properties_array_iter) !=
         DBUS_TYPE_INVALID) {
    DBusMessageIter properties_dict_iter, variant_iter;
    const char* key;
    int type;

    dbus_message_iter_recurse(properties_array_iter, &properties_dict_iter);

    dbus_message_iter_get_basic(&properties_dict_iter, &key);
    dbus_message_iter_next(&properties_dict_iter);

    dbus_message_iter_recurse(&properties_dict_iter, &variant_iter);
    type = dbus_message_iter_get_arg_type(&variant_iter);

    if (type == DBUS_TYPE_STRING || type == DBUS_TYPE_OBJECT_PATH) {
      const char* value;

      dbus_message_iter_get_basic(&variant_iter, &value);

      if (strcmp(key, "Adapter") == 0) {
        free(device->adapter_obj_path);
        device->adapter_obj_path = strdup(value);
      } else if (strcmp(key, "Address") == 0) {
        free(device->address);
        device->address = strdup(value);
      } else if (strcmp(key, "Alias") == 0) {
        free(device->name);
        device->name = strdup(value);
      }

    } else if (type == DBUS_TYPE_UINT32) {
      uint32_t value;

      dbus_message_iter_get_basic(&variant_iter, &value);

      if (strcmp(key, "Class") == 0) {
        device->bluetooth_class = value;
      }

    } else if (type == DBUS_TYPE_BOOLEAN) {
      int value;

      dbus_message_iter_get_basic(&variant_iter, &value);

      if (strcmp(key, "Paired") == 0) {
        device->paired = value;
      } else if (strcmp(key, "Trusted") == 0) {
        device->trusted = value;
      } else if (strcmp(key, "Connected") == 0) {
        cras_bt_device_set_connected(device, value);
        watch_needed =
            device->connected &&
            cras_bt_device_supports_profile(device, CRAS_SUPPORTED_PROFILES);
      }

    } else if (strcmp(dbus_message_iter_get_signature(&variant_iter), "as") ==
                   0 &&
               strcmp(key, "UUIDs") == 0) {
      DBusMessageIter uuid_array_iter;
      unsigned int profiles = 0;

      dbus_message_iter_recurse(&variant_iter, &uuid_array_iter);
      while (dbus_message_iter_get_arg_type(&uuid_array_iter) !=
             DBUS_TYPE_INVALID) {
        const char* uuid;

        dbus_message_iter_get_basic(&uuid_array_iter, &uuid);
        profiles |= cras_bt_device_profile_from_uuid(uuid);

        dbus_message_iter_next(&uuid_array_iter);
      }

      /* If updated properties includes new audio profiles and
       * device is connected, we need to start the connection
       * watcher. This is needed because on some bluetooth
       * devices, supported profiles do not present when
       * device interface is added and they are updated later.
       */
      if (cras_bt_device_set_supported_profiles(device, profiles)) {
        watch_needed = device->connected;
      }
    }

    dbus_message_iter_next(properties_array_iter);
  }

  while (invalidated_array_iter &&
         dbus_message_iter_get_arg_type(invalidated_array_iter) !=
             DBUS_TYPE_INVALID) {
    const char* key;

    dbus_message_iter_get_basic(invalidated_array_iter, &key);

    if (strcmp(key, "Adapter") == 0) {
      free(device->adapter_obj_path);
      device->adapter_obj_path = NULL;
    } else if (strcmp(key, "Address") == 0) {
      free(device->address);
      device->address = NULL;
    } else if (strcmp(key, "Alias") == 0) {
      free(device->name);
      device->name = NULL;
    } else if (strcmp(key, "Class") == 0) {
      device->bluetooth_class = 0;
    } else if (strcmp(key, "Paired") == 0) {
      device->paired = 0;
    } else if (strcmp(key, "Trusted") == 0) {
      device->trusted = 0;
    } else if (strcmp(key, "Connected") == 0) {
      device->connected = 0;
    } else if (strcmp(key, "UUIDs") == 0) {
      device->profiles = device->hidden_profiles;
    }

    dbus_message_iter_next(invalidated_array_iter);
  }

  if (watch_needed) {
    cras_bt_policy_start_connection_watch(device);
  }
}

/* Converts bluetooth address string into sockaddr structure. The address
 * string is expected of the form 1A:2B:3C:4D:5E:6F, and each of the six
 * hex values will be parsed into sockaddr in inverse order.
 * Args:
 *    str - The string version of bluetooth address
 *    addr - The struct to be filled with converted address
 */
static int bt_address(const char* str, struct sockaddr* addr) {
  int i;

  if (strlen(str) != 17) {
    syslog(LOG_ERR, "Invalid bluetooth address %s", str);
    return -EINVAL;
  }

  memset(addr, 0, sizeof(*addr));
  addr->sa_family = AF_BLUETOOTH;
  for (i = 5; i >= 0; i--) {
    addr->sa_data[i] = (unsigned char)strtol(str, NULL, 16);
    str += 3;
  }

  return 0;
}

static int apply_hfp_offload_codec_settings(int fd, uint8_t codec) {
  struct bt_codecs* codecs;
  uint8_t buffer[255];
  int err;

  syslog(LOG_INFO, "apply hfp offload codec settings: codecid(%d)", codec);
  memset(buffer, 0x00, sizeof(buffer));

  codecs = (void*)buffer;

  switch (codec) {
    case HFP_CODEC_ID_CVSD:
      codecs->codecs[0].id = HCI_CONFIG_CODEC_ID_FORMAT_CVSD;
      break;
    case HFP_CODEC_ID_MSBC:
      codecs->codecs[0].id = HCI_CONFIG_CODEC_ID_FORMAT_MSBC;
      break;
    default:
      return -EINVAL;
  }

  codecs->num_codecs = 1;
  codecs->codecs[0].data_path_id = HCI_CONFIG_DATA_PATH_ID_OFFLOAD;
  codecs->codecs[0].num_caps = 0x00;

  err = setsockopt(fd, SOL_BLUETOOTH, BT_CODEC, codecs, sizeof(buffer));
  if (err < 0) {
    /* Fallback setting for kukui cases. The socket option BT_CODEC
     * is not supported on Bluetooth kernel <= v4.19
     */
    if (errno == ENOPROTOOPT) {
      syslog(LOG_WARNING,
             "BT_CODEC socket is not supported; fallback to normal setting");
      return -ENOPROTOOPT;
    }
    /* Fallback setting for kukui-kernelnext cases. The experimental
     * flag of Offload Codecs is not enabled on Bluetooth kernel
     * >= 5.10
     */
    if (errno == EOPNOTSUPP) {
      syslog(LOG_WARNING,
             "Offload is not enabled in BT kernel; fallback to normal setting");
      return -EOPNOTSUPP;
    }
    syslog(LOG_WARNING, "Failed to set codec: %s (%d)", cras_strerror(errno),
           err);
    return err;
  }

  syslog(LOG_INFO, "Successfully applied codec settings");

  return err;
}

// Apply codec specific settings to the socket fd.
static int apply_codec_settings(int fd, uint8_t codec) {
  struct bt_voice voice;
  uint32_t pkt_status;

  syslog(LOG_INFO, "apply hfp HCI codec settings: codecid(%d)", codec);

  memset(&voice, 0, sizeof(voice));
  if (codec == HFP_CODEC_ID_CVSD) {
    return 0;
  }

  if (codec != HFP_CODEC_ID_MSBC) {
    syslog(LOG_WARNING, "Unsupported codec %d", codec);
    return -EOPNOTSUPP;
  }

  voice.setting = BT_VOICE_TRANSPARENT;

  if (setsockopt(fd, SOL_BLUETOOTH, BT_VOICE, &voice, sizeof(voice)) < 0) {
    syslog(LOG_WARNING, "Failed to apply voice setting");
    return -errno;
  }

  pkt_status = 1;
  if (setsockopt(fd, SOL_BLUETOOTH, BT_PKT_STATUS, &pkt_status,
                 sizeof(pkt_status))) {
    syslog(LOG_WARNING, "Failed to enable BT_PKT_STATUS");
  }
  return 0;
}

int cras_bt_device_sco_connect(struct cras_bt_device* device,
                               int codec,
                               bool use_offload) {
  int sk = 0, err = 0;
  struct sockaddr addr;
  struct cras_bt_adapter* adapter;
  struct timespec timeout = {1, 0};
  struct pollfd pollfd;

  adapter = cras_bt_device_adapter(device);
  if (!adapter) {
    syslog(LOG_WARNING, "No adapter found for device %s at SCO connect",
           cras_bt_device_object_path(device));
    goto error;
  }

  sk = socket(PF_BLUETOOTH, SOCK_SEQPACKET | O_NONBLOCK | SOCK_CLOEXEC,
              BTPROTO_SCO);
  if (sk < 0) {
    syslog(LOG_ERR, "Failed to create socket: %s (%d)", cras_strerror(errno),
           errno);
    cras_server_metrics_hfp_sco_connection_error(
        CRAS_METRICS_SCO_SKT_OPEN_ERROR);
    return -errno;
  }

  // Bind to local address
  err = bt_address(cras_bt_adapter_address(adapter), &addr);
  if (err < 0) {
    goto error;
  }
  if (bind(sk, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
    syslog(LOG_ERR, "Failed to bind socket: %s (%d)", cras_strerror(errno),
           errno);
    err = -errno;
    goto error;
  }

  // Connect to remote in nonblocking mode
  fcntl(sk, F_SETFL, O_NONBLOCK);
  err = bt_address(cras_bt_device_address(device), &addr);
  if (err < 0) {
    goto error;
  }

  if (use_offload) {
    err = apply_hfp_offload_codec_settings(sk, codec);
  }
  if (!use_offload || err == -ENOPROTOOPT || err == -EOPNOTSUPP) {
    err = apply_codec_settings(sk, codec);
  }
  if (err) {
    goto error;
  }

  err = connect(sk, (struct sockaddr*)&addr, sizeof(addr));
  if (err && errno != EINPROGRESS) {
    syslog(LOG_WARNING, "Failed to connect: %s (%d)", cras_strerror(errno),
           errno);
    cras_server_metrics_hfp_sco_connection_error(
        CRAS_METRICS_SCO_SKT_CONNECT_ERROR);
    err = -errno;
    goto error;
  }

  pollfd.fd = sk;
  pollfd.events = POLLOUT;

  err = ppoll(&pollfd, 1, &timeout, NULL);
  if (err <= 0) {
    syslog(LOG_WARNING, "Connect SCO: poll for writable timeout");
    cras_server_metrics_hfp_sco_connection_error(
        CRAS_METRICS_SCO_SKT_POLL_TIMEOUT);
    err = -errno;
    goto error;
  }

  if (pollfd.revents & (POLLERR | POLLHUP)) {
    /* If SCO encounters Different Transaction Collision (0x2a)
     * err this poll would fail immediately but actually worth a
     * retry. See cras_iodev_list for retry after INIT_DEV_DELAY_MS.
     * TODO(hychao): Investigate how to tell between the fatal
     * errors and the temporary errors.
     */
    syslog(LOG_WARNING, "SCO socket error, revents: %u. Suspend in %u seconds",
           pollfd.revents, SCO_SUSPEND_DELAY_MS);
    cras_server_metrics_hfp_sco_connection_error(
        CRAS_METRICS_SCO_SKT_POLL_ERR_HUP);
    cras_bt_policy_schedule_suspend(device, SCO_SUSPEND_DELAY_MS,
                                    HFP_SCO_SOCKET_ERROR);
    goto error;
  }

  /*
   * SCO error Different Transaction Collision (0x2a) might have happened
   * earlier and later the SCO connection succeeds in a retry. Cancel any
   * timer scheduled for suspend.
   */
  cras_bt_policy_cancel_suspend(device);
  cras_server_metrics_hfp_sco_connection_error(CRAS_METRICS_SCO_SKT_SUCCESS);
  BTLOG(btlog, BT_SCO_CONNECT, 1, sk);
  return sk;

error:
  BTLOG(btlog, BT_SCO_CONNECT, 0, sk);
  if (sk) {
    close(sk);
  }
  return err;
}

int cras_bt_device_sco_packet_size(struct cras_bt_device* device,
                                   int sco_socket,
                                   int codec) {
  struct sco_options so;
  socklen_t len = sizeof(so);
  struct cras_bt_adapter* adapter;
  uint32_t wbs_pkt_len = 0;
  socklen_t optlen = sizeof(wbs_pkt_len);

  adapter = cras_bt_adapter_get(device->adapter_obj_path);

  if (!adapter) {
    return -ENODEV;
  }

  if (cras_bt_adapter_on_usb(adapter)) {
    if (codec == HFP_CODEC_ID_MSBC) {
      // BT_SNDMTU and BT_RCVMTU return the same value.
      if (getsockopt(sco_socket, SOL_BLUETOOTH, BT_SNDMTU, &wbs_pkt_len,
                     &optlen)) {
        syslog(LOG_WARNING, "Failed to get BT_SNDMTU");
      }

      return (wbs_pkt_len > 0) ? wbs_pkt_len : USB_MSBC_PKT_SIZE;
    } else {
      return USB_CVSD_PKT_SIZE;
    }
  }

  // For non-USB cases, query the SCO MTU from driver.
  if (getsockopt(sco_socket, SOL_SCO, SCO_OPTIONS, &so, &len) < 0) {
    syslog(LOG_WARNING, "Get SCO options error: %s", cras_strerror(errno));
    return DEFAULT_SCO_PKT_SIZE;
  }
  return so.mtu;
}

void cras_bt_device_set_use_hardware_volume(struct cras_bt_device* device,
                                            int use_hardware_volume) {
  device->use_hardware_volume = use_hardware_volume;
  bt_io_manager_set_use_hardware_volume(device->bt_io_mgr, use_hardware_volume);
}

int cras_bt_device_get_use_hardware_volume(struct cras_bt_device* device) {
  return device->use_hardware_volume;
}

void cras_bt_device_update_hardware_volume(struct cras_bt_device* device,
                                           int volume) {
  /* Check if this BT device is okay to use hardware volume. If not
   * then ignore the reported volume change event.
   */
  if (!cras_bt_device_get_use_hardware_volume(device)) {
    return;
  }

  bt_io_manager_update_hardware_volume(device->bt_io_mgr, volume);
}

int cras_bt_device_sco_handle(int sco_socket) {
  struct sco_conninfo info;
  socklen_t len = sizeof(info);

  // Query the SCO handle from kernel.
  if (getsockopt(sco_socket, SOL_SCO, SCO_CONNINFO, &info, &len) < 0) {
    syslog(LOG_WARNING, "Get SCO handle error: %s", cras_strerror(errno));
    return -errno;
  }
  return info.hci_handle;
}

int cras_bt_device_report_hfp_start_stop_status(struct cras_bt_device* device,
                                                bool status,
                                                int sco_handle) {
  DBusMessage* method_call;
  DBusMessageIter message_iter;
  dbus_bool_t hfp_status = status;

  method_call =
      dbus_message_new_method_call(BLUEZ_SERVICE, BLUEZ_CHROMIUM_OBJ_PATH,
                                   BLUEZ_INTERFACE_METRICS, "ReportHfpStatus");

  if (!method_call) {
    return -ENOMEM;
  }

  dbus_message_iter_init_append(method_call, &message_iter);
  dbus_message_iter_append_basic(&message_iter, DBUS_TYPE_BOOLEAN, &hfp_status);
  dbus_message_iter_append_basic(&message_iter, DBUS_TYPE_INT32, &sco_handle);

  if (!dbus_connection_send(device->conn, method_call, NULL)) {
    dbus_message_unref(method_call);
    return -ENOMEM;
  }

  dbus_message_unref(method_call);
  return 0;
}

void cras_bt_device_hfp_reconnect(struct cras_bt_device* device) {
  cras_bt_policy_switch_profile(device->bt_io_mgr);
}
