/* Copyright 2022 The ChromiumOS Authors.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <assert.h>

extern "C" {
#include "sr_bt_util_stub.h"
}

namespace {

static int cras_sr_bt_is_enabled = 0;

}  // namespace

/* Helper functions for testing */

void enable_cras_sr_bt() {
  cras_sr_bt_is_enabled = 1;
}

void disable_cras_sr_bt() {
  cras_sr_bt_is_enabled = 0;
}

/* Fake implementation of cras_bt_sr */

int cras_sr_bt_can_be_enabled() {
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
