/*
 * Copyright 2023 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <stdbool.h>

#include "cras_featured.h"
#include "cras_features.h"

bool get_hfp_offload_feature_enabled()
{
	return cras_feature_enabled(CrOSLateBootAudioHFPOffload);
}

bool get_hfp_mic_sr_feature_enabled()
{
	return cras_feature_enabled(CrOSLateBootAudioHFPMicSR);
}

bool get_flexible_loopback_feature_enabled()
{
	return cras_feature_enabled(CrOSLateBootAudioFlexibleLoopback);
}
