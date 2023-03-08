/* Copyright 2013 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "cras/src/server/cras_hfp_ag_profile.h"

#include <stdint.h>
#include <sys/socket.h>
#include <syslog.h>
#include <unistd.h>

#include "cras/src/server/cras_a2dp_endpoint.h"
#include "cras/src/server/cras_bt_adapter.h"
#include "cras/src/server/cras_bt_constants.h"
#include "cras/src/server/cras_bt_log.h"
#include "cras/src/server/cras_bt_profile.h"
#include "cras/src/server/cras_features.h"
#include "cras/src/server/cras_hfp_alsa_iodev.h"
#include "cras/src/server/cras_hfp_iodev.h"
#include "cras/src/server/cras_iodev_list.h"
#include "cras/src/server/cras_sco.h"
#include "cras/src/server/cras_server_metrics.h"
#include "cras/src/server/cras_system_state.h"
#include "packet_status_logger.h"
#include "third_party/utlist/utlist.h"

#define HFP_AG_PROFILE_NAME "Hands-Free Voice gateway"
#define HFP_AG_PROFILE_PATH "/org/chromium/Cras/Bluetooth/HFPAG"
#define HFP_VERSION 0x0107

// The supported features value in +BSRF command response of HFP AG in CRAS
#define BSRF_SUPPORTED_FEATURES (AG_ENHANCED_CALL_STATUS | AG_HF_INDICATORS)

// The "SupportedFeatures" attribute value of HFP AG service record in CRAS.
#define SDP_SUPPORTED_FEATURES FEATURES_AG_WIDE_BAND_SPEECH

// Object representing the audio gateway role for HFP.
struct audio_gateway {
  // The input iodev for HFP.
  struct cras_iodev* idev;
  // The output iodev for HFP.
  struct cras_iodev* odev;
  // The cras_sco object for SCO audio.
  struct cras_sco* sco;
  // The service level connection.
  struct hfp_slc_handle* slc_handle;
  // The bt device associated with this audio gateway.
  struct cras_bt_device* device;
  // The number of retries left to delay starting
  // the hfp audio gateway to wait for a2dp connection.
  int a2dp_delay_retries;
  // The dbus connection used to send message to bluetoothd.
  DBusConnection* conn;
  // The flag for recording if device is initialized with
  // SCO PCM.
  bool sco_pcm_used;
  struct audio_gateway *prev, *next;
};

static struct audio_gateway* connected_ags;
static struct packet_status_logger wbs_logger;

static bool is_sco_pcm_supported() {
  return (cras_iodev_list_get_sco_pcm_iodev(CRAS_STREAM_INPUT) ||
          cras_iodev_list_get_sco_pcm_iodev(CRAS_STREAM_OUTPUT));
}

static bool is_sco_pcm_used() {
  /* If board config "bluetooth:hfp_offload_finch_applied" is specified,
   * check the feature state from Chrome Feature Service to determine
   * whether to use HFP offload path; otherwise, always choose HFP offload
   * path.
   */
  if (cras_system_get_bt_hfp_offload_finch_applied()) {
    return cras_feature_enabled(CrOSLateBootAudioHFPOffload);
  }

  return true;
}

static void destroy_audio_gateway(struct audio_gateway* ag) {
  DL_DELETE(connected_ags, ag);

  cras_server_metrics_hfp_battery_indicator(
      hfp_slc_get_hf_supports_battery_indicator(ag->slc_handle));

  if (ag->sco_pcm_used) {
    if (ag->idev) {
      hfp_alsa_iodev_destroy(ag->idev);
    }
    if (ag->odev) {
      hfp_alsa_iodev_destroy(ag->odev);
    }
  } else {
    if (ag->idev) {
      hfp_iodev_destroy(ag->idev);
    }
    if (ag->odev) {
      hfp_iodev_destroy(ag->odev);
    }
  }

  if (ag->sco) {
    if (cras_sco_running(ag->sco)) {
      cras_sco_stop(ag->sco);
    }
    cras_sco_destroy(ag->sco);
  }
  if (ag->slc_handle) {
    hfp_slc_destroy(ag->slc_handle);
  }

  free(ag);
}

// Checks if there already a audio gateway connected for device.
static int has_audio_gateway(struct cras_bt_device* device) {
  struct audio_gateway* ag;
  DL_FOREACH (connected_ags, ag) {
    if (ag->device == device) {
      return 1;
    }
  }
  return 0;
}

static void cras_hfp_ag_release(struct cras_bt_profile* profile) {
  struct audio_gateway* ag;

  DL_FOREACH (connected_ags, ag) {
    destroy_audio_gateway(ag);
  }
}

// Callback triggered when SLC is initialized.
static int cras_hfp_ag_slc_initialized(struct hfp_slc_handle* handle) {
  struct audio_gateway* ag;

  DL_SEARCH_SCALAR(connected_ags, ag, slc_handle, handle);
  if (!ag) {
    return -EINVAL;
  }

  /* Log if the hands-free device supports WBS or not. Assuming the
   * codec negotiation feature means the WBS capability on headset.
   */
  cras_server_metrics_hfp_wideband_support(
      hfp_slc_get_hf_codec_negotiation_supported(handle));

  /* Log the final selected codec given that codec negotiation is
   * supported.
   */
  if (hfp_slc_get_hf_codec_negotiation_supported(handle) &&
      hfp_slc_get_ag_codec_negotiation_supported(handle)) {
    cras_server_metrics_hfp_wideband_selected_codec(
        hfp_slc_get_selected_codec(handle));
  }

  // Defer the starting of audio gateway to bt_device.
  return cras_bt_device_audio_gateway_initialized(ag->device);
}

static int cras_hfp_ag_slc_disconnected(struct hfp_slc_handle* handle) {
  struct audio_gateway* ag;

  DL_SEARCH_SCALAR(connected_ags, ag, slc_handle, handle);
  if (!ag) {
    return -EINVAL;
  }

  cras_bt_device_notify_profile_dropped(ag->device,
                                        CRAS_BT_DEVICE_PROFILE_HFP_HANDSFREE);
  destroy_audio_gateway(ag);
  return 0;
}

static int check_for_conflict_ag(struct cras_bt_device* new_connected) {
  struct audio_gateway* ag;

  // Check if there's already an A2DP/HFP device.
  DL_FOREACH (connected_ags, ag) {
    if (cras_bt_device_has_a2dp(ag->device)) {
      return -EBUSY;
    }
  }

  // Check if there's already an A2DP-only device.
  if (cras_a2dp_connected_device() &&
      cras_bt_device_supports_profile(new_connected,
                                      CRAS_BT_DEVICE_PROFILE_A2DP_SINK)) {
    return -EBUSY;
  }

  return 0;
}

int cras_hfp_ag_remove_conflict(struct cras_bt_device* device) {
  struct audio_gateway* ag;

  DL_FOREACH (connected_ags, ag) {
    if (ag->device == device) {
      continue;
    }
    cras_bt_device_notify_profile_dropped(ag->device,
                                          CRAS_BT_DEVICE_PROFILE_HFP_HANDSFREE);
    destroy_audio_gateway(ag);
  }
  return 0;
}

static int cras_hfp_ag_new_connection(DBusConnection* conn,
                                      struct cras_bt_profile* profile,
                                      struct cras_bt_device* device,
                                      int rfcomm_fd) {
  struct cras_bt_adapter* adapter;
  struct audio_gateway* ag;
  int ag_features, ret;

  BTLOG(btlog, BT_HFP_NEW_CONNECTION, 0, 0);

  if (has_audio_gateway(device)) {
    syslog(LOG_WARNING, "Audio gateway exists when %s connects for profile %s",
           cras_bt_device_name(device), profile->name);
    close(rfcomm_fd);
    return 0;
  }

  ret = check_for_conflict_ag(device);
  if (ret < 0) {
    return ret;
  }

  ag = (struct audio_gateway*)calloc(1, sizeof(*ag));
  ag->device = device;
  ag->conn = conn;
  ag->sco_pcm_used = false;

  adapter = cras_bt_device_adapter(device);
  /*
   * If the WBS enabled flag is set and adapter reports wbs capability
   * then add codec negotiation feature.
   * TODO(hychao): AND the two conditions to let bluetooth daemon
   * control whether to turn on WBS feature.
   */
  ag_features = BSRF_SUPPORTED_FEATURES;
  if (cras_system_get_bt_wbs_enabled() && adapter &&
      cras_bt_adapter_wbs_supported(adapter)) {
    ag_features |= AG_CODEC_NEGOTIATION;
  }

  ag->slc_handle =
      hfp_slc_create(rfcomm_fd, ag_features, device,
                     cras_hfp_ag_slc_initialized, cras_hfp_ag_slc_disconnected);
  DL_APPEND(connected_ags, ag);
  return 0;
}

static void cras_hfp_ag_request_disconnection(struct cras_bt_profile* profile,
                                              struct cras_bt_device* device) {
  struct audio_gateway* ag;

  BTLOG(btlog, BT_HFP_REQUEST_DISCONNECT, 0, 0);

  DL_FOREACH (connected_ags, ag) {
    if (ag->slc_handle && ag->device == device) {
      cras_bt_device_notify_profile_dropped(
          ag->device, CRAS_BT_DEVICE_PROFILE_HFP_HANDSFREE);
      destroy_audio_gateway(ag);
    }
  }
}

static void cras_hfp_ag_cancel(struct cras_bt_profile* profile) {}

static struct cras_bt_profile cras_hfp_ag_profile = {
    .name = HFP_AG_PROFILE_NAME,
    .object_path = HFP_AG_PROFILE_PATH,
    .uuid = HFP_AG_UUID,
    .version = HFP_VERSION,
    .role = NULL,
    .features = SDP_SUPPORTED_FEATURES,
    .record = NULL,
    .release = cras_hfp_ag_release,
    .new_connection = cras_hfp_ag_new_connection,
    .request_disconnection = cras_hfp_ag_request_disconnection,
    .cancel = cras_hfp_ag_cancel};

int cras_hfp_ag_profile_create(DBusConnection* conn) {
  return cras_bt_add_profile(conn, &cras_hfp_ag_profile);
}

int cras_hfp_ag_profile_destroy(DBusConnection* conn) {
  cras_bt_unregister_profile(conn, &cras_hfp_ag_profile);
  return cras_bt_rm_profile(conn, &cras_hfp_ag_profile);
}

int cras_hfp_ag_start(struct cras_bt_device* device) {
  struct audio_gateway* ag;
  bool sco_pcm_supported;

  DL_SEARCH_SCALAR(connected_ags, ag, device, device);
  if (ag == NULL) {
    return -EEXIST;
  }

  /*
   * There is chance that bluetooth stack notifies us about remote
   * device's capability incrementally in multiple events. That could
   * cause hfp_ag_start be called more than once. Check if the input
   * HFP iodev is already created so we don't re-create HFP resources.
   */
  if (ag->idev) {
    return 0;
  }

  ag->sco = cras_sco_create(device);

  sco_pcm_supported = is_sco_pcm_supported();
  ag->sco_pcm_used = sco_pcm_supported && is_sco_pcm_used();

  BTLOG(btlog, BT_AUDIO_GATEWAY_START, sco_pcm_supported, ag->sco_pcm_used);

  if (ag->sco_pcm_used) {
    struct cras_iodev *in_aio, *out_aio;

    in_aio = cras_iodev_list_get_sco_pcm_iodev(CRAS_STREAM_INPUT);
    out_aio = cras_iodev_list_get_sco_pcm_iodev(CRAS_STREAM_OUTPUT);

    ag->idev = hfp_alsa_iodev_create(in_aio, ag->device, ag->slc_handle,
                                     ag->sco, NULL);
    ag->odev = hfp_alsa_iodev_create(out_aio, ag->device, ag->slc_handle,
                                     ag->sco, NULL);
  } else {
    cras_sco_set_wbs_logger(ag->sco, &wbs_logger);
    ag->idev = hfp_iodev_create(CRAS_STREAM_INPUT, ag->device, ag->slc_handle,
                                ag->sco);
    ag->odev = hfp_iodev_create(CRAS_STREAM_OUTPUT, ag->device, ag->slc_handle,
                                ag->sco);
  }

  if (!ag->idev && !ag->odev) {
    destroy_audio_gateway(ag);
    return -ENOMEM;
  }

  return 0;
}

void cras_hfp_ag_suspend_connected_device(struct cras_bt_device* device) {
  struct audio_gateway* ag;

  DL_SEARCH_SCALAR(connected_ags, ag, device, device);
  if (ag) {
    destroy_audio_gateway(ag);
  }
}

struct hfp_slc_handle* cras_hfp_ag_get_active_handle() {
  /* Returns the first handle for HFP qualification. In future we
   * might want this to return the HFP device user is selected. */
  return connected_ags ? connected_ags->slc_handle : NULL;
}

struct hfp_slc_handle* cras_hfp_ag_get_slc(struct cras_bt_device* device) {
  struct audio_gateway* ag;
  DL_FOREACH (connected_ags, ag) {
    if (ag->device == device) {
      return ag->slc_handle;
    }
  }
  return NULL;
}

struct packet_status_logger* cras_hfp_ag_get_wbs_logger() {
  return &wbs_logger;
}
