// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Generated from files in cras/src/server/rust in adhd.

#ifndef CRAS_FEATURE_TIER_H_
#define CRAS_FEATURE_TIER_H_

typedef struct {
	bool sr_bt_supported;
} cras_feature_tier;

/**
 * Initialize the cras feature tier struct.
 * On error, a negative error code is returned.
 */
int cras_feature_tier_init(cras_feature_tier *out, const char *board_name,
			   const char *cpu_name);

#endif /* CRAS_FEATURE_TIER_H_ */
