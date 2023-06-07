// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cras/src/tests/scoped_features_override.h"
#include "gtest/gtest.h"

TEST(ScopedFeaturesOverrideTest, Override) {
  cras_features_init();

  EXPECT_FALSE(cras_feature_enabled(CrOSLateBootDisabledByDefault));
  EXPECT_TRUE(cras_feature_enabled(CrOSLateBootEnabledByDefault));

  {
    // Override EnabledByDefault to disabled, DisabledByDefault to enabled.
    ScopedFeaturesOverride override1({CrOSLateBootDisabledByDefault},
                                     {CrOSLateBootEnabledByDefault});
    EXPECT_TRUE(cras_feature_enabled(CrOSLateBootDisabledByDefault));
    EXPECT_FALSE(cras_feature_enabled(CrOSLateBootEnabledByDefault));
    {
      // Override DisabledByDefault to disabled.
      // EnabledByDefault should not be changed.
      ScopedFeaturesOverride override2({}, {CrOSLateBootDisabledByDefault});
      EXPECT_FALSE(cras_feature_enabled(CrOSLateBootDisabledByDefault));
      EXPECT_FALSE(cras_feature_enabled(CrOSLateBootEnabledByDefault));
    }
    EXPECT_TRUE(cras_feature_enabled(CrOSLateBootDisabledByDefault));
    EXPECT_FALSE(cras_feature_enabled(CrOSLateBootEnabledByDefault));
  }

  EXPECT_FALSE(cras_feature_enabled(CrOSLateBootDisabledByDefault));
  EXPECT_TRUE(cras_feature_enabled(CrOSLateBootEnabledByDefault));

  cras_features_deinit();
}

class ScopedFeaturesOverrideInFixture : public ::testing::Test {
 public:
  ScopedFeaturesOverrideInFixture()
      : feature_overrides_({CrOSLateBootDisabledByDefault},
                           {CrOSLateBootEnabledByDefault}) {}

 private:
  ScopedFeaturesOverride feature_overrides_;
};

TEST_F(ScopedFeaturesOverrideInFixture, Override) {
  EXPECT_TRUE(cras_feature_enabled(CrOSLateBootDisabledByDefault));
  EXPECT_FALSE(cras_feature_enabled(CrOSLateBootEnabledByDefault));
}
