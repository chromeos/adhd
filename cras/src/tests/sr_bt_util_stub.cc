/* Copyright 2022 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <assert.h>

extern "C" {
#include "cras/src/tests/sr_bt_util_stub.h"
}

namespace {

static enum CRAS_SR_BT_CAN_BE_ENABLED_STATUS cras_sr_bt_is_enabled =
    CRAS_SR_BT_CAN_BE_ENABLED_STATUS_FEATURE_DISABLED;

}  // namespace

// Helper functions for testing

void enable_cras_sr_bt() {
  cras_sr_bt_is_enabled = CRAS_SR_BT_CAN_BE_ENABLED_STATUS_OK;
}

void disable_cras_sr_bt() {
  cras_sr_bt_is_enabled = CRAS_SR_BT_CAN_BE_ENABLED_STATUS_FEATURE_DISABLED;
}

// Fake implementation of cras_bt_sr

enum CRAS_SR_BT_CAN_BE_ENABLED_STATUS cras_sr_bt_can_be_enabled() {
  return cras_sr_bt_is_enabled;
}

struct cras_sr_model_spec cras_sr_bt_get_model_spec(
    enum cras_sr_bt_model model) {
  struct cras_sr_model_spec spec = {};
  switch (model) {
    case SR_BT_NBS: {
      spec.num_frames_per_run = 480;
      spec.num_channels = 1;
      spec.input_sample_rate = 8000;
      spec.output_sample_rate = 24000;
      break;
    };
    case SR_BT_WBS: {
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
                             bool is_enabled) {}
