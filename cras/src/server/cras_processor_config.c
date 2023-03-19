// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cras/src/server/cras_processor_config.h"

#include "cras/src/server/cras_features.h"
#include "cras/src/server/rust/include/cras_processor.h"

enum CrasProcessorEffect cras_processor_get_effect() {
  if (cras_feature_enabled(CrOSLateBootAudioAPNoiseCancellation)) {
    // TODO: Figure out the effect based on system toggle.
    return NoiseCancellation;
  } else {
    return NoEffects;
  }
}
