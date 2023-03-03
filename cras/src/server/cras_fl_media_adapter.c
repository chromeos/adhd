/* Copyright 2022 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
#include "cras/src/server/cras_fl_media_adapter.h"

#include <assert.h>
#include <errno.h>
#include <regex.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <syslog.h>

#include "cras/src/server/cras_bt_log.h"
#include "cras/src/server/cras_bt_policy.h"
#include "cras/src/server/cras_fl_media.h"

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

static int validate_hfp_codec_capability(int32_t hfp_cap) {
  if (0 <= hfp_cap && hfp_cap <= (FL_CODEC_CVSD | FL_CODEC_MSBC)) {
    return 0;
  }
  return -EINVAL;
}

int handle_on_bluetooth_device_added(struct fl_media* active_fm,
                                     const char* addr,
                                     const char* name,
                                     struct cras_fl_a2dp_codec_config* codecs,
                                     int32_t hfp_cap,
                                     bool abs_vol_supported) {
  int rc = validate_bluetooth_device_address(addr);
  if (rc) {
    syslog(LOG_WARNING, "Erroneous bluetooth device address match %d", rc);
    return rc;
  }

  rc = validate_hfp_codec_capability(hfp_cap);
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
    active_fm->hfp =
        cras_floss_hfp_create(active_fm, addr, name, hfp_cap & FL_CODEC_MSBC);

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
  return 0;
}

int handle_on_bluetooth_device_removed(struct fl_media* active_fm,
                                       const char* addr) {
  assert(active_fm != NULL);
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
  assert(active_fm != NULL);
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
  }
  return 0;
}

int handle_on_absolute_volume_changed(struct fl_media* active_fm,
                                      uint8_t volume) {
  assert(active_fm != NULL);
  if (!active_fm->bt_io_mgr || !active_fm->a2dp) {
    syslog(LOG_WARNING, "No active a2dp device. Skip the volume update");
    return -EINVAL;
  }
  if (active_fm->a2dp) {
    BTLOG(btlog, BT_A2DP_UPDATE_VOLUME, volume, 0);
    bt_io_manager_update_hardware_volume(
        active_fm->bt_io_mgr,
        cras_floss_a2dp_convert_volume(active_fm->a2dp, volume));
  }
  return 0;
}

int handle_on_hfp_volume_changed(struct fl_media* active_fm,
                                 const char* addr,
                                 uint8_t volume) {
  assert(active_fm != NULL);
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
  BTLOG(btlog, BT_HFP_UPDATE_SPEAKER_GAIN, volume, 0);
  bt_io_manager_update_hardware_volume(active_fm->bt_io_mgr,
                                       cras_floss_hfp_convert_volume(volume));
  return 0;
}

int handle_on_hfp_audio_disconnected(struct fl_media* active_fm,
                                     const char* addr) {
  assert(active_fm != NULL);
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
  cras_floss_hfp_possibly_reconnect(active_fm->hfp);
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
