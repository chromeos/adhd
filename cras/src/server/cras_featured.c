/* Copyright 2022 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
#include <stdbool.h>
#include <syslog.h>

#include <featured/c_feature_library.h>

#include "cras_featured.h"

static bool get_feature_enabled(const struct VariationsFeature *feature)
{
	CFeatureLibrary lib = CFeatureLibraryNew();
	int enabled = CFeatureLibraryIsEnabledBlocking(lib, feature);
	syslog(LOG_DEBUG, "Chrome Feature Service: %s = %d", feature->name,
	       enabled);
	CFeatureLibraryDelete(lib);

	return enabled;
}

const struct VariationsFeature AUDIO_HFP_OFFLOAD_FEATURE = {
	.name = "CrOSLateBootAudioHFPOffload",
	.default_state = FEATURE_DISABLED_BY_DEFAULT,
};

bool get_hfp_offload_feature_enabled()
{
	return get_feature_enabled(&AUDIO_HFP_OFFLOAD_FEATURE);
}

const struct VariationsFeature AUDIO_HFP_MIC_SR = {
	.name = "CrOSLateBootAudioHFPMicSR",
	.default_state = FEATURE_DISABLED_BY_DEFAULT,
};

bool get_hfp_mic_sr_feature_enabled()
{
	return get_feature_enabled(&AUDIO_HFP_MIC_SR);
}
