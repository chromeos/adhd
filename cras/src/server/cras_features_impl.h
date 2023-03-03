/*
 * Copyright 2023 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef CRAS_SRC_SERVER_CRAS_FEATURES_IMPL_H_
#define CRAS_SRC_SERVER_CRAS_FEATURES_IMPL_H_

#include <stdbool.h>

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

bool cras_features_backend_get_enabled(const struct cras_feature* feature);

enum cras_feature_id cras_feature_get_id(const struct cras_feature* feature);

#ifdef __cplusplus
}
#endif

#endif
