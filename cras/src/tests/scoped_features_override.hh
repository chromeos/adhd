// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CRAS_SRC_TESTS_SCOPED_FEATURES_OVERRIDE_HH_
#define CRAS_SRC_TESTS_SCOPED_FEATURES_OVERRIDE_HH_

#include <utility>
#include <vector>

#include "cras/platform/features/features.h"

// ScopedFeaturesOverride overrides the enabled features upon construction.
// Upon destruct the original feature configuration is restored.
//
// If multiple instances of this class are used in a nested fashion, they
// should be destroyed in the opposite order.
//
// See scoped_features_override_unittest.cc for example usage.
class ScopedFeaturesOverride final {
 public:
  ScopedFeaturesOverride(
      const std::vector<cras_feature_id>& enabled_features,
      const std::vector<cras_feature_id>& disabled_features = {});

  ~ScopedFeaturesOverride();

 private:
  std::vector<std::pair<cras_feature_id, bool>> restore_enabled_;
};

#endif  // CRAS_SRC_TESTS_SCOPED_FEATURES_OVERRIDE_HH_
