// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Generated from files in cras/src/server/rust in adhd.
// clang-format off

#ifdef __cplusplus
extern "C" {
#endif

#ifndef CRAS_FEATURE_TIER_H_
#define CRAS_FEATURE_TIER_H_

#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>

/**
 * Support status for CRAS features.
 */
struct cras_feature_tier {
  bool sr_bt_supported;
};

/**
 * Initialize the cras feature tier struct.
 * On error, a negative error code is returned.
 */
int cras_feature_tier_init(struct cras_feature_tier *out,
                           const char *board_name,
                           const char *cpu_name);

#endif /* CRAS_FEATURE_TIER_H_ */

#ifdef __cplusplus
}
#endif
