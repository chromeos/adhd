// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cras/src/server/cras_processor_config.h"

#include <stdbool.h>
#include <stdint.h>

#include "cras/server/platform/features/features.h"
#include "cras/server/s2/s2.h"
#include "cras/src/server/cras_iodev.h"
#include "cras/src/server/cras_system_state.h"
#include "cras/src/server/rust/include/cras_processor.h"
#include "cras_types.h"

enum CrasProcessorEffect cras_processor_get_effect(
    bool nc_provided_by_ap,
    const struct cras_iodev* iodev,
    uint64_t effects) {
  if (cras_feature_enabled(CrOSLateBootAudioAecRequiredForCrasProcessor) &&
      !(effects & APM_ECHO_CANCELLATION)) {
    return NoEffects;
  }

  const bool beamforming_supported =
      cras_s2_get_beamforming_supported() && iodev->active_node &&
      iodev->active_node->position == NODE_POSITION_INTERNAL;

  // StyleTransfer
  if (iodev->active_node->nc_providers & CRAS_NC_PROVIDER_AST &&
      cras_s2_get_style_transfer_allowed() &&
      ((effects & CLIENT_CONTROLLED_VOICE_ISOLATION &&  // client controlled.
        effects & VOICE_ISOLATION) ||
       (!(effects & CLIENT_CONTROLLED_VOICE_ISOLATION) &&  // system controlled.
        cras_system_get_style_transfer_enabled())) &&
      !beamforming_supported) {  // no beamforming.
    return StyleTransfer;
  }

  // NoiseCancellation
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
    return NoiseCancellation;
  }
  return NoEffects;
}
