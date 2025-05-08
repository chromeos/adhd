// Copyright 2025 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Generated from files in cras/src/server/rust in adhd.
// clang-format off

#ifdef __cplusplus
extern "C" {
#endif

#ifndef CRAS_SERVER_PLATFORM_FEATURES_FEATURES_BACKEND_H_
#define CRAS_SERVER_PLATFORM_FEATURES_FEATURES_BACKEND_H_

#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include "cras/server/platform/features/features.h"

/**
 * Initialize the cras_features backend.
 */
void cras_features_backend_init(cras_features_notify_changed changed_callback);

/**
 * Clean up resources associated with the cras_features backend.
 */
void cras_features_backend_deinit(void);

/**
 * Get whether the feature is enabled.
 */
bool cras_features_backend_get_enabled(enum cras_feature_id id);

#endif  /* CRAS_SERVER_PLATFORM_FEATURES_FEATURES_BACKEND_H_ */

#ifdef __cplusplus
}
#endif
