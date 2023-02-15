// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cras/src/tests/scoped_features_override.h"

#include "cras/src/server/cras_features.h"
#include "cras/src/server/cras_features_override.h"

ScopedFeaturesOverride::ScopedFeaturesOverride(
    const std::vector<cras_feature_id>& enabled_features,
    const std::vector<cras_feature_id>& disabled_features) {
  // Construct list of features to restore.
  for (cras_feature_id id : enabled_features) {
    restore_enabled_.emplace_back(id, cras_feature_enabled(id));
  }
  for (cras_feature_id id : disabled_features) {
    restore_enabled_.emplace_back(id, cras_feature_enabled(id));
  }

  // Override features.
  for (cras_feature_id id : enabled_features) {
    cras_features_set_override(id, true);
  }
  for (cras_feature_id id : disabled_features) {
    cras_features_set_override(id, false);
  }
}

ScopedFeaturesOverride::~ScopedFeaturesOverride() {
  for (auto [id, enabled] : restore_enabled_) {
    cras_features_set_override(id, enabled);
  }
}
