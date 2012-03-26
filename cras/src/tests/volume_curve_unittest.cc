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
  struct cras_volume_curve *curve;
  curve = cras_volume_curve_create_default();
  ASSERT_NE(static_cast<struct cras_volume_curve *>(NULL), curve);
  EXPECT_EQ(0 - 50 * 75, curve->get_dBFS(curve, 50));
  EXPECT_EQ(0, curve->get_dBFS(curve, 100));
  EXPECT_EQ(0 - 100 * 75, curve->get_dBFS(curve, 0));
  EXPECT_EQ(0 - 25 * 75, curve->get_dBFS(curve, 75));
  cras_volume_curve_destroy(curve);
}

TEST(VolumeCurve, SteppedCurve) {
  struct cras_volume_curve *curve;
  curve = cras_volume_curve_create_simple_step(-600, 75);
  ASSERT_NE(static_cast<struct cras_volume_curve *>(NULL), curve);
  EXPECT_EQ(-600 - 50 * 75, curve->get_dBFS(curve, 50));
  EXPECT_EQ(-600, curve->get_dBFS(curve, 100));
  EXPECT_EQ(-600 - 100 * 75, curve->get_dBFS(curve, 0));
  EXPECT_EQ(-600 - 25 * 75, curve->get_dBFS(curve, 75));
  cras_volume_curve_destroy(curve);
}

}  //  namespace

int main(int argc, char **argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
