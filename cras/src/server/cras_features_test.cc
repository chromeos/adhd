// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cras/src/server/cras_features_impl.h"
#include "gtest/gtest.h"

TEST(Features, GetId) {
  EXPECT_EQ(cras_feature_get_id(&features[3]), 3);
  EXPECT_EQ(cras_feature_get_id(&features[CrOSLateBootAudioFlexibleLoopback]),
            CrOSLateBootAudioFlexibleLoopback);
}
