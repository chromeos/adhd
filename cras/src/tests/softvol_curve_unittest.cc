// Copyright 2015 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <gtest/gtest.h>
#include <stdio.h>

extern "C" {
#include "cras/src/server/softvol_curve.h"
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

TEST(SoftvolCurveTest, InputNodeGainToScaler) {
  for (long dBFS = 0; dBFS <= 2000; ++dBFS) {
    float scaler = convert_softvol_scaler_from_dB(dBFS);
    long dBFS_from_scaler = convert_dBFS_from_softvol_scaler(scaler);
    EXPECT_EQ(dBFS, dBFS_from_scaler);
  }
}

TEST(SoftvolCurveTest, SoftvolGetScalerDefault) {
  unsigned int volume_index = 0;
  for (; volume_index <= MAX_VOLUME; volume_index++) {
    EXPECT_EQ(softvol_get_scaler_default(volume_index),
              softvol_scalers[volume_index]);
  }
  volume_index = MAX_VOLUME + 1;
  EXPECT_EQ(softvol_get_scaler_default(volume_index),
            softvol_scalers[MAX_VOLUME]);
}

class SoftvolCurveTestSuite : public testing::Test {
 protected:
  virtual void SetUp() {
    volume_index_ = 0;
    curve_ = cras_volume_curve_create_default();
    scalers_ = softvol_build_from_curve(curve_);
  }

  virtual void TearDown() {
    cras_volume_curve_destroy(curve_);
    free(scalers_);
  }
  unsigned int volume_index_;
  struct cras_volume_curve* curve_;
  float* scalers_;
};

TEST_F(SoftvolCurveTestSuite, SoftvolGetScaler) {
  for (; volume_index_ <= MAX_VOLUME; volume_index_++) {
    EXPECT_EQ(softvol_get_scaler(scalers_, volume_index_),
              scalers_[volume_index_]);
  }
  volume_index_ = MAX_VOLUME + 1;
  EXPECT_EQ(softvol_get_scaler(scalers_, volume_index_), scalers_[MAX_VOLUME]);
}

}  //  namespace
