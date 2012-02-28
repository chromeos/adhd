// Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stdio.h>
#include <gtest/gtest.h>

extern "C" {
#include "cras_volume_curve.h"
}

namespace {

TEST(VolumeCurve, DefaultCurve) {
  EXPECT_EQ(-5000, cras_volume_curve_get_dBFS_for_index(50));
  EXPECT_EQ(0, cras_volume_curve_get_dBFS_for_index(100));
  EXPECT_EQ(-10000, cras_volume_curve_get_dBFS_for_index(0));
  EXPECT_EQ(-2500, cras_volume_curve_get_dBFS_for_index(75));
}

}  //  namespace

int main(int argc, char **argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
