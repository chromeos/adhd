/* Copyright 2022 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "cras/src/server/cras_sr_bt_util.h"

#include <assert.h>
#include <stdbool.h>
#include <stdio.h>

#include "cras/src/server/cras_features.h"
#include "cras/src/server/cras_iodev.h"
#include "cras/src/server/cras_server_metrics.h"
#include "cras/src/server/cras_system_state.h"
#include "cras/src/server/rust/include/cras_dlc.h"

enum CRAS_SR_BT_CAN_BE_ENABLED_STATUS cras_sr_bt_can_be_enabled() {
  if (!cras_system_get_force_sr_bt_enabled()) {
    if (!cras_system_get_sr_bt_supported()) {
      return CRAS_SR_BT_CAN_BE_ENABLED_STATUS_FEATURE_UNSUPPORTED;
    }
    if (!cras_feature_enabled(CrOSLateBootAudioHFPMicSR)) {
      return CRAS_SR_BT_CAN_BE_ENABLED_STATUS_FEATURE_DISABLED;
    }
  }
  // else: feature is force enabled.

  if (!cras_dlc_sr_bt_is_available()) {
    return CRAS_SR_BT_CAN_BE_ENABLED_STATUS_DLC_UNAVAILABLE;
  }
  return CRAS_SR_BT_CAN_BE_ENABLED_STATUS_OK;
}

struct cras_sr_model_spec cras_sr_bt_get_model_spec(
    enum cras_sr_bt_model model) {
  const char* dlc_root = cras_dlc_sr_bt_get_root();
  struct cras_sr_model_spec spec = {};
  switch (model) {
    case SR_BT_NBS: {
      snprintf(spec.model_path, CRAS_SR_MODEL_PATH_CAPACITY, "%s/%s", dlc_root,
               "btnb.tflite");
      spec.num_frames_per_run = 480;
      spec.num_channels = 1;
      spec.input_sample_rate = 8000;
      spec.output_sample_rate = 24000;
      break;
    };
    case SR_BT_WBS: {
      snprintf(spec.model_path, CRAS_SR_MODEL_PATH_CAPACITY, "%s/%s", dlc_root,
               "btwb.tflite");
      spec.num_frames_per_run = 480;
      spec.num_channels = 1;
      spec.input_sample_rate = 16000;
      spec.output_sample_rate = 24000;
      break;
    }
    default:
      assert(0 && "unknown model type.");
  }
  return spec;
}

void cras_sr_bt_send_uma_log(struct cras_iodev* iodev,
                             const enum CRAS_SR_BT_CAN_BE_ENABLED_STATUS status,
                             bool is_enabled) {
  enum CRAS_METRICS_HFP_MIC_SR_STATUS log_status =
      CRAS_METRICS_HFP_MIC_SR_ENABLE_SUCCESS;
  switch (status) {
    case CRAS_SR_BT_CAN_BE_ENABLED_STATUS_OK: {
      log_status = is_enabled ? CRAS_METRICS_HFP_MIC_SR_ENABLE_SUCCESS
                              : CRAS_METRICS_HFP_MIC_SR_ENABLE_FAILED;
      break;
    }
    case CRAS_SR_BT_CAN_BE_ENABLED_STATUS_FEATURE_UNSUPPORTED: {
      log_status = CRAS_METRICS_HFP_MIC_SR_FEATURE_UNSUPPORTED;
      break;
    }
    case CRAS_SR_BT_CAN_BE_ENABLED_STATUS_FEATURE_DISABLED: {
      log_status = CRAS_METRICS_HFP_MIC_SR_FEATURE_DISABLED;
      break;
    }
    case CRAS_SR_BT_CAN_BE_ENABLED_STATUS_DLC_UNAVAILABLE: {
      log_status = CRAS_METRICS_HFP_MIC_SR_DLC_UNAVAILABLE;
      break;
    }
    default:
      assert(0 && "unknown status.");
  }
  cras_server_metrics_hfp_mic_sr_status(iodev, log_status);
}
