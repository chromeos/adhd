/*
 * Copyright 2023 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "cras/src/server/cras_features_impl.h"

#include <syslog.h>

#include "cras/src/server/cras_features.h"
#include "cras/src/server/cras_features_override.h"

static struct cras_feature features[NUM_FEATURES] = {
    [CrosLateBootAudioTestFeatureFlag] =
        {
            .name = "CrosLateBootAudioTestFeatureFlag",
            .default_enabled = false,
        },
    [CrOSLateBootAudioHFPOffload] =
        {
            .name = "CrOSLateBootAudioHFPOffload",
            .default_enabled = false,
        },
    [CrOSLateBootAudioHFPMicSR] =
        {
            .name = "CrOSLateBootAudioHFPMicSR",
            .default_enabled = false,
        },
    [CrOSLateBootAudioFlexibleLoopback] =
        {
            .name = "CrOSLateBootAudioFlexibleLoopback",
            .default_enabled = false,
        },
    [CrOSLateBootAudioAPNoiseCancellation] =
        {
            .name = "CrOSLateBootAudioAPNoiseCancellation",
            .default_enabled = false,
        },
    [CrOSLateBootCrasSplitAlsaUSBInternal] = {
        .name = "CrOSLateBootCrasSplitAlsaUSBInternal",
        .default_enabled = true,
    }};

bool cras_feature_enabled(enum cras_feature_id id) {
  if (id >= NUM_FEATURES || id < 0) {
    syslog(LOG_ERR, "invalid feature ID: %d", id);
    return false;
  }
  if (features[id].overridden) {
    bool enabled = features[id].overridden_enabled;
    syslog(LOG_DEBUG, "feature %s overriden enabled = %d", features[id].name,
           enabled);
    return enabled;
  }
  bool enabled = cras_features_backend_get_enabled(&features[id]);
  syslog(LOG_DEBUG, "feature %s enabled = %d", features[id].name, enabled);
  return enabled;
}

void cras_features_set_override(enum cras_feature_id id, bool enabled) {
  features[id].overridden = true;
  features[id].overridden_enabled = enabled;
}

void cras_features_unset_override(enum cras_feature_id id) {
  features[id].overridden = false;
}
