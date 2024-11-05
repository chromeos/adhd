/* Copyright 2022 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
#include "cras/src/server/cras_fl_media_adapter.h"

#include <errno.h>
#include <regex.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <syslog.h>

#include "cras/common/check.h"
#include "cras/server/platform/features/features.h"
#include "cras/src/server/cras_a2dp_manager.h"
#include "cras/src/server/cras_bt_io.h"
#include "cras/src/server/cras_bt_log.h"
#include "cras/src/server/cras_bt_policy.h"
#include "cras/src/server/cras_fl_manager.h"
#include "cras/src/server/cras_fl_media.h"
#include "cras/src/server/cras_hfp_manager.h"
#include "cras/src/server/cras_lea_manager.h"
#include "cras/src/server/cras_server_metrics.h"
#include "cras_types.h"

static int validate_bluetooth_device_address(const char* addr) {
  if (!addr) {
    syslog(LOG_WARNING, "Empty bluetooth device address");
    return -ENOMEM;
  }
  /* An example of addr looks like A0:1A:7D:DA:71:11
   * 6 sets of hexadecimal numbers
   * Total length: 17
   */
  regex_t regex;
  char buffer[100] = {};
  int rc = regcomp(&regex, "^(([0-9A-F]{2}):){5}([0-9A-F]{2})$",
                   REG_EXTENDED | REG_ICASE);
  if (rc) {
    regerror(rc, &regex, buffer, 100);
    syslog(LOG_ERR, "Failed to compile regex [%s]", buffer);
    return -EINVAL;
  }
  rc = regexec(&regex, addr, 0, NULL, 0);
  regfree(&regex);
  if (rc) {
    syslog(LOG_WARNING, "Invalid bluetooth device address %s", addr);
    return -EINVAL;
  }
  return 0;
}

static int validate_hfp_codec_format(int32_t hfp_cap) {
  if (FL_HFP_CODEC_FORMAT_NONE <= hfp_cap &&
      hfp_cap < FL_HFP_CODEC_FORMAT_UNKNOWN) {
    return 0;
  }
  return -EINVAL;
}

static uint16_t get_redacted_bluetooth_device_address(const char* addr) {
  int rc = validate_bluetooth_device_address(addr);
  if (rc) {
    syslog(LOG_WARNING, "%s: invalid address, rc=%d.", __func__, rc);
    return 0;
  }

  // Let's be extra sure.
  CRAS_CHECK(strlen(addr) == 17);

  char* ptr;

  uint16_t hi = strtoul(addr + 12, &ptr, 16);

  // This must be true because we have validated the address, otherwise
  // something has gone really wrong.
  CRAS_CHECK(ptr == addr + 14);

  uint16_t lo = strtoul(ptr + 1, NULL, 16);

  return (hi << 8) | lo;
}

int handle_on_lea_group_connected(struct fl_media* active_fm,
                                  const char* name,
                                  int group_id) {
  syslog(LOG_DEBUG, "%s(name=%s, group_id=%d)", __func__, name, group_id);

  if (!cras_feature_enabled(CrOSLateBootBluetoothAudioLEAudioOnly)) {
    syslog(LOG_WARNING, "%s: ignored due to LEAudioOnly flag.", __func__);
    return -EPERM;
  }

  if (!active_fm) {
    syslog(LOG_WARNING, "%s: Floss media is inactive.", __func__);
    return -EINVAL;
  }

  if (!active_fm->lea) {
    active_fm->lea = cras_floss_lea_create(active_fm);
  }

  cras_floss_lea_add_group(active_fm->lea, name, group_id);
  cras_floss_lea_set_active(active_fm->lea, group_id, 1);

  BTLOG(btlog, BT_LEA_GROUP_CONNECTED, group_id, 0);

  return 0;
}

int handle_on_lea_group_disconnected(struct fl_media* active_fm, int group_id) {
  syslog(LOG_DEBUG, "%s(group_id=%d)", __func__, group_id);

  if (!cras_feature_enabled(CrOSLateBootBluetoothAudioLEAudioOnly)) {
    return -EPERM;
  }

  if (!active_fm) {
    syslog(LOG_WARNING, "%s: Floss media is inactive.", __func__);
    return -EINVAL;
  }

  cras_floss_lea_remove_group(active_fm->lea, group_id);

  if (!cras_floss_lea_has_connected_group(active_fm->lea)) {
    cras_floss_lea_destroy(active_fm->lea);
  }

  BTLOG(btlog, BT_LEA_GROUP_DISCONNECTED, group_id, 0);

  return 0;
}

int handle_on_lea_audio_conf(struct fl_media* active_fm,
                             uint8_t direction,
                             int group_id,
                             uint32_t snk_audio_location,
                             uint32_t src_audio_location,
                             uint16_t available_contexts) {
  syslog(LOG_DEBUG,
         "%s(direction=%u, group_id=%d, snk_audio_location=%u, "
         "src_audio_location=%u, available_contexts=%u)",
         __func__, direction, group_id, snk_audio_location, src_audio_location,
         available_contexts);

  if (!cras_feature_enabled(CrOSLateBootBluetoothAudioLEAudioOnly)) {
    return -EPERM;
  }

  if (!active_fm) {
    syslog(LOG_WARNING, "%s: Floss media is inactive.", __func__);
    return -EINVAL;
  }

  cras_floss_lea_audio_conf_updated(active_fm->lea, direction, group_id,
                                    snk_audio_location, src_audio_location,
                                    available_contexts);

  BTLOG(btlog, BT_LEA_AUDIO_CONF_UPDATED, group_id,
        (direction << 16) | available_contexts);

  return 0;
}

int handle_on_lea_group_status(struct fl_media* active_fm,
                               int group_id,
                               int status) {
  syslog(LOG_DEBUG, "%s(group_id=%d, status=%d)", __func__, group_id, status);

  if (!cras_feature_enabled(CrOSLateBootBluetoothAudioLEAudioOnly)) {
    return -EPERM;
  }

  if (status != FL_LEA_GROUP_INACTIVE && status != FL_LEA_GROUP_ACTIVE &&
      status != FL_LEA_GROUP_TURNED_IDLE_DURING_CALL) {
    syslog(LOG_WARNING, "%s: Unknown status %d", __func__, status);
    return -EINVAL;
  }

  BTLOG(btlog, BT_LEA_GROUP_STATUS, group_id, status);

  return 0;
}

int handle_on_lea_group_node_status(struct fl_media* active_fm,
                                    const char* addr,
                                    int group_id,
                                    int status) {
  syslog(LOG_DEBUG, "%s(addr=%s, group_id=%d, status=%d)", __func__, addr,
         group_id, status);

  BTLOG(btlog, BT_LEA_GROUP_NODE_STATUS, group_id, status);

  return 0;
}

// Note: The current implementation assumes the group id is fixed for
// each device during the lifetime of its connection. Since we treat groups
// as the integral unit of audio device, only the first VC connection matters.
// This will need to be reworked if the assumption breaks.
int handle_on_lea_vc_connected(struct fl_media* active_fm,
                               const char* addr,
                               int group_id) {
  int rc = validate_bluetooth_device_address(addr);
  if (rc) {
    syslog(LOG_WARNING, "Erroneous bluetooth device address match %d", rc);
    return rc;
  }

  syslog(LOG_DEBUG, "%s(addr=%s, group_id=%d)", __func__, addr, group_id);

  BTLOG(btlog, BT_LEA_SET_ABS_VOLUME_SUPPORT, group_id,
        get_redacted_bluetooth_device_address(addr));

  return cras_floss_lea_set_support_absolute_volume(active_fm->lea, group_id,
                                                    true);
}

int handle_on_lea_group_volume_changed(struct fl_media* active_fm,
                                       int group_id,
                                       uint8_t volume) {
  syslog(LOG_DEBUG, "%s(group_id=%d, volume=%u)", __func__, group_id, volume);

  BTLOG(btlog, BT_LEA_GROUP_VOLUME_CHANGED, group_id, volume);

  return cras_floss_lea_update_group_volume(active_fm->lea, group_id, volume);
}

int handle_on_bluetooth_device_added(struct fl_media* active_fm,
                                     const char* addr,
                                     const char* name,
                                     struct cras_fl_a2dp_codec_config* codecs,
                                     int32_t hfp_cap,
                                     bool abs_vol_supported) {
  if (cras_feature_enabled(CrOSLateBootBluetoothAudioLEAudioOnly)) {
    syslog(LOG_WARNING, "%s: ignored due to LEAudioOnly flag.", __func__);
    return -EPERM;
  }

  int rc = validate_bluetooth_device_address(addr);
  if (rc) {
    syslog(LOG_WARNING, "Erroneous bluetooth device address match %d", rc);
    return rc;
  }

  rc = validate_hfp_codec_format(hfp_cap);
  if (rc) {
    syslog(LOG_WARNING, "Invalid hfp_cap: %d", hfp_cap);
    return rc;
  }

  int a2dp_avail = cras_floss_get_a2dp_enabled() && codecs != NULL;
  int hfp_avail = cras_floss_get_hfp_enabled() && hfp_cap;

  if (!a2dp_avail & !hfp_avail) {
    return -EINVAL;
  }

  if (!active_fm->bt_io_mgr) {
    active_fm->bt_io_mgr = bt_io_manager_create();
    if (!active_fm->bt_io_mgr) {
      return -ENOMEM;
    }
  }

  if (a2dp_avail) {
    syslog(LOG_DEBUG, "A2DP device added.");
    if (active_fm->a2dp) {
      syslog(LOG_WARNING, "Multiple A2DP devices added, remove the older");
      bt_io_manager_remove_iodev(active_fm->bt_io_mgr,
                                 cras_floss_a2dp_get_iodev(active_fm->a2dp));
      cras_floss_a2dp_destroy(active_fm->a2dp);
    }
    active_fm->a2dp = cras_floss_a2dp_create(active_fm, addr, name, codecs);

    if (active_fm->a2dp) {
      cras_floss_a2dp_set_support_absolute_volume(active_fm->a2dp,
                                                  abs_vol_supported);
      bt_io_manager_append_iodev(active_fm->bt_io_mgr,
                                 cras_floss_a2dp_get_iodev(active_fm->a2dp),
                                 CRAS_BT_FLAG_A2DP);
    } else {
      syslog(LOG_WARNING, "Failed to create the cras_a2dp_manager");
    }
  }

  if (hfp_avail) {
    syslog(LOG_DEBUG, "HFP device added with capability %d.", hfp_cap);
    if (active_fm->hfp) {
      syslog(LOG_WARNING, "Multiple HFP devices added, remove the older");
      floss_media_hfp_suspend(active_fm);
    }
    active_fm->hfp = cras_floss_hfp_create(active_fm, addr, name, hfp_cap);

    if (active_fm->hfp) {
      bt_io_manager_append_iodev(active_fm->bt_io_mgr,
                                 cras_floss_hfp_get_input_iodev(active_fm->hfp),
                                 CRAS_BT_FLAG_HFP);
      bt_io_manager_append_iodev(
          active_fm->bt_io_mgr, cras_floss_hfp_get_output_iodev(active_fm->hfp),
          CRAS_BT_FLAG_HFP);
    } else {
      syslog(LOG_WARNING, "Failed to create the cras_hfp_manager");
    }
  }
  if (active_fm->a2dp != NULL || active_fm->hfp != NULL) {
    bt_io_manager_set_nodes_plugged(active_fm->bt_io_mgr, 1);
    BTLOG(btlog, BT_DEV_ADDED, a2dp_avail, hfp_avail | hfp_cap << 1);
  }

  bt_io_manager_set_telephony_use(active_fm->bt_io_mgr,
                                  active_fm->telephony_use);
  return 0;
}

int handle_on_bluetooth_device_removed(struct fl_media* active_fm,
                                       const char* addr) {
  CRAS_CHECK(active_fm != NULL);
  if (!active_fm->bt_io_mgr) {
    syslog(LOG_WARNING, "No device has been added.");
    return -EINVAL;
  }

  if ((active_fm->hfp &&
       strcmp(cras_floss_hfp_get_addr(active_fm->hfp), addr) != 0) ||
      (active_fm->a2dp &&
       strcmp(cras_floss_a2dp_get_addr(active_fm->a2dp), addr) != 0)) {
    syslog(LOG_WARNING, "Non-active device(%s). Ignore the device remove",
           addr);
    return -EINVAL;
  }

  BTLOG(btlog, BT_DEV_REMOVED, 0, 0);
  bt_io_manager_set_nodes_plugged(active_fm->bt_io_mgr, 0);
  if (active_fm->a2dp) {
    floss_media_a2dp_suspend(active_fm);
  }
  if (active_fm->hfp) {
    floss_media_hfp_suspend(active_fm);
  }

  return 0;
}

int handle_on_absolute_volume_supported_changed(struct fl_media* active_fm,
                                                bool abs_vol_supported) {
  CRAS_CHECK(active_fm != NULL);
  if (!active_fm->bt_io_mgr || !active_fm->a2dp) {
    syslog(LOG_WARNING,
           "No active a2dp device. Skip the absolute volume support change");
    return -EINVAL;
  }
  if (active_fm->a2dp) {
    cras_floss_a2dp_set_support_absolute_volume(active_fm->a2dp,
                                                abs_vol_supported);
    bt_io_manager_set_use_hardware_volume(active_fm->bt_io_mgr,
                                          abs_vol_supported);

    if (abs_vol_supported) {
      struct cras_iodev* iodev = cras_floss_a2dp_get_iodev(active_fm->a2dp);
      // Under certain conditions, this AVRCP capability update event could
      // occur while there is an ongoing stream, in which case there needs
      // to be an explicit |set_volume| request to synchronize the volume.
      if (iodev && iodev->active_node) {
        // This is a workaround for headsets that cache the previous volume,
        // which Fluoride will read from and prevent dup requests. By setting 0
        // immediately before the actual volume, we guarantee the volume is set.
        cras_floss_a2dp_set_volume(active_fm->a2dp, 0);
        cras_floss_a2dp_set_volume(active_fm->a2dp, iodev->active_node->volume);
      }
    }
  }
  return 0;
}

int handle_on_absolute_volume_changed(struct fl_media* active_fm,
                                      uint8_t volume) {
  CRAS_CHECK(active_fm != NULL);
  if (!active_fm->bt_io_mgr || !active_fm->a2dp) {
    syslog(LOG_WARNING, "No active a2dp device. Skip the volume update");
    return -EINVAL;
  }
  if (active_fm->hfp && cras_floss_hfp_get_fd(active_fm->hfp) != -1) {
    syslog(LOG_WARNING, "AVRCP volume update received while HFP is streaming.");
  }
  if (active_fm->a2dp) {
    BTLOG(btlog, BT_A2DP_UPDATE_VOLUME, volume, 0);
    bt_io_manager_update_hardware_volume(
        active_fm->bt_io_mgr,
        cras_floss_a2dp_convert_volume(active_fm->a2dp, volume),
        CRAS_BT_FLAG_A2DP);
  }
  return 0;
}

int handle_on_hfp_volume_changed(struct fl_media* active_fm,
                                 const char* addr,
                                 uint8_t volume) {
  CRAS_CHECK(active_fm != NULL);
  int rc = validate_bluetooth_device_address(addr);
  if (rc) {
    syslog(LOG_WARNING, "Erroneous bluetooth device address match %d", rc);
    return rc;
  }
  if (!active_fm->hfp || !active_fm->bt_io_mgr ||
      strcmp(cras_floss_hfp_get_addr(active_fm->hfp), addr) != 0) {
    syslog(LOG_WARNING, "non-active hfp device(%s). Skip the volume update",
           addr);
    return -EINVAL;
  }
  if (active_fm->a2dp && cras_floss_a2dp_get_fd(active_fm->a2dp) != -1) {
    syslog(LOG_WARNING, "HFP volume update received while a2dp is streaming.");
  }

  BTLOG(btlog, BT_HFP_UPDATE_SPEAKER_GAIN, volume, 0);
  bt_io_manager_update_hardware_volume(active_fm->bt_io_mgr,
                                       cras_floss_hfp_convert_volume(volume),
                                       CRAS_BT_FLAG_HFP);
  return 0;
}

int handle_on_hfp_audio_disconnected(struct fl_media* active_fm,
                                     const char* addr) {
  CRAS_CHECK(active_fm != NULL);
  int rc = validate_bluetooth_device_address(addr);
  if (rc) {
    syslog(LOG_WARNING, "Erroneous bluetooth device address match %d", rc);
    return rc;
  }
  if (!active_fm->hfp || !active_fm->bt_io_mgr ||
      strcmp(cras_floss_hfp_get_addr(active_fm->hfp), addr) != 0) {
    syslog(LOG_WARNING,
           "non-active hfp device(%s). Skip handling disconnection event",
           addr);
    return -EINVAL;
  }
  BTLOG(btlog, BT_HFP_AUDIO_DISCONNECTED, 0, 0);
  cras_floss_hfp_handle_audio_disconnection(active_fm->hfp);
  return 0;
}

int handle_on_hfp_telephony_event(struct fl_media* active_fm,
                                  const char* addr,
                                  uint8_t event,
                                  uint8_t state) {
  CRAS_CHECK(active_fm != NULL);
  enum CRAS_BT_HFP_TELEPHONY_EVENT telephony_event =
      (enum CRAS_BT_HFP_TELEPHONY_EVENT)event;
  if (!active_fm->bt_io_mgr) {
    return -EINVAL;
  }
  switch (telephony_event) {
    case CRAS_BT_HFP_TELEPHONY_EVENT_UHID_OPEN:
      cras_server_metrics_hfp_telephony_event(HFP_TELEPHONY_UHID_OPEN);
      bt_io_manager_set_telephony_use(active_fm->bt_io_mgr, true);
      break;
    case CRAS_BT_HFP_TELEPHONY_EVENT_UHID_CLOSE:
      bt_io_manager_set_telephony_use(active_fm->bt_io_mgr, false);
      break;
    default:
      break;
  }
  BTLOG(btlog, BT_HFP_TELEPHONY_EVENT, event, state);
  return 0;
}

void fl_media_destroy(struct fl_media** active_fm) {
  // Clean up iodev when BT forced to stop.
  if (*active_fm) {
    floss_media_a2dp_suspend(*active_fm);
    floss_media_hfp_suspend(*active_fm);

    if ((*active_fm)->bt_io_mgr) {
      cras_bt_policy_remove_io_manager((*active_fm)->bt_io_mgr);
      bt_io_manager_destroy((*active_fm)->bt_io_mgr);
    }
    free(*active_fm);
    *active_fm = NULL;
  }
}
