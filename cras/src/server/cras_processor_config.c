// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cras/src/server/cras_processor_config.h"

#include <stdbool.h>
#include <stdint.h>

#include "cras/server/platform/features/features.h"
#include "cras/server/s2/s2.h"
#include "cras/src/server/cras_system_state.h"
#include "cras/src/server/rust/include/cras_processor.h"
#include "cras_types.h"

enum CrasProcessorEffect cras_processor_get_effect(bool nc_provided_by_ap,
                                                   bool beamforming_supported,
                                                   uint64_t effects) {
  if (cras_feature_enabled(CrOSLateBootAudioAecRequiredForCrasProcessor) &&
      !(effects & APM_ECHO_CANCELLATION)) {
    return NoEffects;
  }
  bool voice_isolation_enabled =
      (effects & CLIENT_CONTROLLED_VOICE_ISOLATION)
          ? (effects & VOICE_ISOLATION)
          : cras_system_get_noise_cancellation_enabled();
  if (nc_provided_by_ap && voice_isolation_enabled &&
      cras_s2_get_ap_nc_allowed()) {
    if (beamforming_supported) {
      // Beamforming is a variant of NoiseCancellation.
      return Beamforming;
    }
    if (cras_system_get_style_transfer_enabled()) {
      // StyleTransfer includes NoiseCancellation.
      return StyleTransfer;
    }
    return NoiseCancellation;
  }
  return NoEffects;
}
