// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cras/src/server/cras_processor_config.h"

#include <stdbool.h>
#include <stdint.h>

#include "cras/platform/features/features.h"
#include "cras/src/server/cras_system_state.h"
#include "cras/src/server/rust/include/cras_processor.h"
#include "cras_types.h"

enum CrasProcessorEffect cras_processor_get_effect(bool nc_provided_by_ap,
                                                   uint64_t effects) {
  bool voice_isolation_enabled =
      (effects & CLIENT_CONTROLLED_VOICE_ISOLATION)
          ? (effects & VOICE_ISOLATION)
          : cras_system_get_noise_cancellation_enabled();
  if (nc_provided_by_ap && voice_isolation_enabled &&
      cras_system_get_ap_noise_cancellation_supported() &&
      cras_feature_enabled(CrOSLateBootAudioAPNoiseCancellation)) {
    return NoiseCancellation;
  }
  return NoEffects;
}
