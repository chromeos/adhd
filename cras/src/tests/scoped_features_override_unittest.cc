// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cras/src/tests/scoped_features_override.h"
#include "gtest/gtest.h"

TEST(ScopedFeaturesOverrideTest, Override) {
  bool initially_enabled =
      cras_feature_enabled(CrOSLateBootAudioTestFeatureFlag);

  {
    ScopedFeaturesOverride override1({CrOSLateBootAudioTestFeatureFlag});
    EXPECT_TRUE(cras_feature_enabled(CrOSLateBootAudioTestFeatureFlag));
    {
      ScopedFeaturesOverride override2({}, {CrOSLateBootAudioTestFeatureFlag});
      EXPECT_FALSE(cras_feature_enabled(CrOSLateBootAudioTestFeatureFlag));
    }
    EXPECT_TRUE(cras_feature_enabled(CrOSLateBootAudioTestFeatureFlag));
  }

  EXPECT_EQ(cras_feature_enabled(CrOSLateBootAudioTestFeatureFlag),
            initially_enabled);
}

class ScopedFeaturesOverrideInFixture : public ::testing::Test {
 public:
  ScopedFeaturesOverrideInFixture()
      : feature_overrides_({CrOSLateBootAudioTestFeatureFlag}) {}

 private:
  ScopedFeaturesOverride feature_overrides_;
};

TEST_F(ScopedFeaturesOverrideInFixture, Override) {
  EXPECT_TRUE(cras_feature_enabled(CrOSLateBootAudioTestFeatureFlag));
}
