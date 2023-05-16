// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cras/src/server/cras_processor_config.h"

#include <stdbool.h>

#include "cras/src/server/cras_features.h"
#include "cras/src/server/cras_system_state.h"
#include "cras/src/server/rust/include/cras_processor.h"

enum CrasProcessorEffect cras_processor_get_effect(bool nc_provided_by_ap) {
  if (nc_provided_by_ap && cras_system_get_noise_cancellation_enabled() &&
      cras_system_get_ap_noise_cancellation_supported() && false) {
    return NoiseCancellation;
  }
  return NoEffects;
}
