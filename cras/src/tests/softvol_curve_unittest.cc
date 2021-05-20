// Copyright 2015 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <gtest/gtest.h>
#include <stdio.h>

extern "C" {
#include "softvol_curve.h"
}

namespace {

static float ABS_ERROR = 0.0000001;

TEST(SoftvolCurveTest, ScalerDecibelConvert) {
  float scaler;
  scaler = convert_softvol_scaler_from_dB(-2000);
  EXPECT_NEAR(scaler, 0.1f, ABS_ERROR);
  scaler = convert_softvol_scaler_from_dB(-1000);
  EXPECT_NEAR(scaler, 0.3162277f, ABS_ERROR);
  scaler = convert_softvol_scaler_from_dB(-4000);
  EXPECT_NEAR(scaler, 0.01f, ABS_ERROR);
  scaler = convert_softvol_scaler_from_dB(-3500);
  EXPECT_NEAR(scaler, 0.0177828f, ABS_ERROR);
}

TEST(SoftvolCurveTest, InputNodeGainToDBFS) {
  for (long gain = 0; gain <= 100; ++gain) {
    long dBFS = convert_dBFS_from_input_node_gain(gain);
    EXPECT_EQ(dBFS, (gain - 50) * ((gain > 50) ? 40 : 80));
    EXPECT_EQ(gain, convert_input_node_gain_from_dBFS(dBFS));
  }
}

TEST(SoftvolCurveTest, InputNodeGainToScaler) {
  for (long gain = 0; gain <= 100; ++gain) {
    long dBFS = convert_dBFS_from_input_node_gain(gain);
    float scaler = convert_softvol_scaler_from_dB(dBFS);
    long dBFS_from_scaler = convert_dBFS_from_softvol_scaler(scaler);
    EXPECT_EQ(dBFS, dBFS_from_scaler);
    long gain_from_scaler = convert_input_node_gain_from_dBFS(dBFS_from_scaler);
    EXPECT_EQ(gain, gain_from_scaler);
  }
}

}  //  namespace

/* Stubs */
extern "C" {}  // extern "C"

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
