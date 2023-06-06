// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cras/platform/features/features_impl.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

TEST(Features, GetId) {
  EXPECT_EQ(cras_feature_get_id(&features[3]), 3);
  EXPECT_EQ(cras_feature_get_id(&features[CrOSLateBootAudioFlexibleLoopback]),
            CrOSLateBootAudioFlexibleLoopback);
}

TEST(Features, Name) {
  for (const struct cras_feature& feature : features) {
    EXPECT_THAT(feature.name, testing::StartsWith("CrOSLateBoot"))
        << "If the feature does not have the correct prefix, it will fail the "
           "prefix check in Chrome and never be enabled.";
  }
}

TEST(Features, GetByName) {
  EXPECT_EQ(CrOSLateBootDisabledByDefault,
            cras_feature_get_by_name("CrOSLateBootDisabledByDefault"));
  EXPECT_EQ(CrOSLateBootUnknown, cras_feature_get_by_name("???"));
  EXPECT_EQ(CrOSLateBootUnknown, cras_feature_get_by_name(NULL));
}
