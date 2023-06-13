// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <featured/c_feature_library.h>

#include "cras/platform/features/features.h"
#include "gtest/gtest.h"

// Mocks.
extern "C" {

int CFeatureLibraryIsEnabledBlockingWithTimeout_result;
int CFeatureLibraryIsEnabledBlockingWithTimeout(
    CFeatureLibrary handle,
    const struct VariationsFeature* const feature,
    int timeout_ms) {
  return CFeatureLibraryIsEnabledBlockingWithTimeout_result;
}

struct timespec clock_gettime_result;
int clock_gettime(clockid_t clockid, struct timespec* tp) {
  *tp = clock_gettime_result;
  return 0;
}
}

TEST(FeaturesBackendFeatured, Caching) {
  // Necessary to test cras_feature_enabled properly.
  EXPECT_EQ(cras_features_init(), 0);

  clock_gettime_result = {0, 0};
  CFeatureLibraryIsEnabledBlockingWithTimeout_result = true;
  EXPECT_EQ(cras_feature_enabled(CrOSLateBootDisabledByDefault), true);

  // Library result changed to false.
  CFeatureLibraryIsEnabledBlockingWithTimeout_result = false;

  for (int i = 0; i < 5; i++) {
    clock_gettime_result = {i, 0};
    // cras_feature_enabled should continue to return true, due to caching
    EXPECT_EQ(cras_feature_enabled(CrOSLateBootDisabledByDefault), true);
  }

  // Cache expired after 5 seconds, should return false
  clock_gettime_result = {5, 0};
  EXPECT_EQ(cras_feature_enabled(CrOSLateBootDisabledByDefault), false);
  clock_gettime_result = {6, 0};
  EXPECT_EQ(cras_feature_enabled(CrOSLateBootDisabledByDefault), false);
}
