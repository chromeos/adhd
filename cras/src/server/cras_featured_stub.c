/* Copyright 2022 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
#include <stdbool.h>

#include "cras_featured.h"

bool get_hfp_offload_feature_enabled()
{
	return true;
}

bool get_hfp_mic_sr_feature_enabled()
{
	return false;
}

int cras_feature_tier_init()
{
	return 0;
}
