/*
 * Copyright 2023 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef CRAS_SRC_SERVER_CRAS_FEATURES_H_
#define CRAS_SRC_SERVER_CRAS_FEATURES_H_

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

enum cras_feature_id {
  CrOSLateBootAudioTestFeatureFlag,
  CrOSLateBootAudioHFPOffload,
  CrOSLateBootAudioHFPMicSR,
  CrOSLateBootAudioFlexibleLoopback,
  CrOSLateBootAudioAPNoiseCancellation,
  CrOSLateBootCrasSplitAlsaUSBInternal,
  NUM_FEATURES,
};

// Initializes CFeatureLibrary.
bool feature_library_initialize();

// Get whether the feature is enabled.
bool cras_feature_enabled(enum cras_feature_id id);

#ifdef __cplusplus
}
#endif

#endif
