/*
 * Copyright 2023 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef CRAS_FEATURES_H_
#define CRAS_FEATURES_H_

#include <stdbool.h>

enum cras_feature_id {
	CrosLateBootAudioTestFeatureFlag,
	CrOSLateBootAudioHFPOffload,
	CrOSLateBootAudioHFPMicSR,
	CrOSLateBootAudioFlexibleLoopback,
	CrOSLateBootAudioAPNoiseCancellation,
	NUM_FEATURES,
};

// Get whether the feature is enabled.
bool cras_feature_enabled(enum cras_feature_id id);

#endif
