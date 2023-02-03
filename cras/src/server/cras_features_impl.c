/*
 * Copyright 2023 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "cras_features.h"
#include "cras_features_impl.h"

#include <syslog.h>

static struct cras_feature features[NUM_FEATURES] = {
	[CrosLateBootAudioTestFeatureFlag] = {
		.name = "CrosLateBootAudioTestFeatureFlag",
		.default_enabled = false,
	},
	[CrOSLateBootAudioHFPOffload] = {
		.name = "CrOSLateBootAudioHFPOffload",
		.default_enabled = false,
	},
	[CrOSLateBootAudioHFPMicSR] = {
		.name = "CrOSLateBootAudioHFPMicSR",
		.default_enabled = false,
	},
	[CrOSLateBootAudioFlexibleLoopback] = {
		.name = "CrOSLateBootAudioFlexibleLoopback",
		.default_enabled = false,
	},
};

bool cras_feature_enabled(enum cras_feature_id id)
{
	if (id >= NUM_FEATURES || id < 0) {
		syslog(LOG_ERR, "invalid feature ID: %d", id);
		return false;
	}
	bool enabled = cras_features_backend_get_enabled(&features[id]);
	syslog(LOG_DEBUG, "feature %s enabled = %d", features[id].name,
	       enabled);
	return enabled;
}
