/*
 * Copyright 2023 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef CRAS_PLATFORM_FEATURES_FEATURES_IMPL_H_
#define CRAS_PLATFORM_FEATURES_FEATURES_IMPL_H_

#include <stdbool.h>

// Include path relative for bindgen.
#include "features.h"

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

enum cras_feature_id cras_feature_get_id(const struct cras_feature* feature);

#ifdef __cplusplus
}
#endif

#endif
