/*
 * Copyright 2023 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef CRAS_PLATFORM_FEATURES_FEATURES_IMPL_H_
#define CRAS_PLATFORM_FEATURES_FEATURES_IMPL_H_

#include <stdbool.h>

#include "cras/platform/features/features.h"
#include "cras/server/main_message.h"

#ifdef __cplusplus
extern "C" {
#endif

struct cras_feature {
  // The name of the feature, used when consulting featured.
  const char* const name;
  // Whether to enable the feature by default.
  const bool default_enabled;

  // Overrides set in cras_features_override.h
  bool overridden;
  bool overridden_enabled;  // Is the feature overridden to be enabled?
};

extern struct cras_feature features[NUM_FEATURES];

// Callback to call from a backend to notify that features changed.
typedef void (*cras_features_notify_changed)();

// Initialize the cras_features backend.
// Returns a negative error code on failure, 0 on success.
int cras_features_backend_init(cras_features_notify_changed changed_callback);

// Clean up resources associated with the cras_features backend.
void cras_features_backend_deinit();

bool cras_features_backend_get_enabled(const struct cras_feature* feature);

enum cras_feature_id cras_feature_get_id(const struct cras_feature* feature);

#ifdef __cplusplus
}
#endif

#endif
