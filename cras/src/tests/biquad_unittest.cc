// Copyright 2019 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <gtest/gtest.h>
#include <math.h>

#include "cras/common/check.h"
#include "cras/src/dsp/biquad.h"

namespace {

TEST(InvalidFrequencyTest, All) {
  struct biquad bq, test_bq;
  float f_over = 1.5;
  float f_under = -0.1;
  double db_gain = 2;
  double A = pow(10.0, db_gain / 40);

  // check response to freq >= 1
  bq = biquad_new_set(BQ_LOWPASS, f_over, 0, db_gain);
  test_bq = {0, 0, 0, 0, 0, 0, 0, 0, 0};
  test_bq.b0 = 1;
  EXPECT_EQ(bq.b0, test_bq.b0);
  EXPECT_EQ(bq.b1, test_bq.b1);
  EXPECT_EQ(bq.b2, test_bq.b2);
  EXPECT_EQ(bq.a1, test_bq.a1);
  EXPECT_EQ(bq.a2, test_bq.a2);

  bq = biquad_new_set(BQ_HIGHPASS, f_over, 0, db_gain);
  test_bq = {0, 0, 0, 0, 0, 0, 0, 0, 0};
  EXPECT_EQ(bq.b0, test_bq.b0);
  EXPECT_EQ(bq.b1, test_bq.b1);
  EXPECT_EQ(bq.b2, test_bq.b2);
  EXPECT_EQ(bq.a1, test_bq.a1);
  EXPECT_EQ(bq.a2, test_bq.a2);

  bq = biquad_new_set(BQ_BANDPASS, f_over, 0, db_gain);
  test_bq = {0, 0, 0, 0, 0, 0, 0, 0, 0};
  EXPECT_EQ(bq.b0, test_bq.b0);
  EXPECT_EQ(bq.b1, test_bq.b1);
  EXPECT_EQ(bq.b2, test_bq.b2);
  EXPECT_EQ(bq.a1, test_bq.a1);
  EXPECT_EQ(bq.a2, test_bq.a2);

  bq = biquad_new_set(BQ_LOWSHELF, f_over, 0, db_gain);
  test_bq = {0, 0, 0, 0, 0, 0, 0, 0, 0};
  test_bq.b0 = A * A;
  EXPECT_EQ(bq.b0, test_bq.b0);
  EXPECT_EQ(bq.b1, test_bq.b1);
  EXPECT_EQ(bq.b2, test_bq.b2);
  EXPECT_EQ(bq.a1, test_bq.a1);
  EXPECT_EQ(bq.a2, test_bq.a2);

  bq = biquad_new_set(BQ_HIGHSHELF, f_over, 0, db_gain);
  test_bq = {0, 0, 0, 0, 0, 0, 0, 0, 0};
  test_bq.b0 = 1;
  EXPECT_EQ(bq.b0, test_bq.b0);
  EXPECT_EQ(bq.b1, test_bq.b1);
  EXPECT_EQ(bq.b2, test_bq.b2);
  EXPECT_EQ(bq.a1, test_bq.a1);
  EXPECT_EQ(bq.a2, test_bq.a2);

  bq = biquad_new_set(BQ_PEAKING, f_over, 0, db_gain);
  test_bq = {0, 0, 0, 0, 0, 0, 0, 0, 0};
  test_bq.b0 = 1;
  EXPECT_EQ(bq.b0, test_bq.b0);
  EXPECT_EQ(bq.b1, test_bq.b1);
  EXPECT_EQ(bq.b2, test_bq.b2);
  EXPECT_EQ(bq.a1, test_bq.a1);
  EXPECT_EQ(bq.a2, test_bq.a2);

  bq = biquad_new_set(BQ_NOTCH, f_over, 0, db_gain);
  test_bq = {0, 0, 0, 0, 0, 0, 0, 0, 0};
  test_bq.b0 = 1;
  EXPECT_EQ(bq.b0, test_bq.b0);
  EXPECT_EQ(bq.b1, test_bq.b1);
  EXPECT_EQ(bq.b2, test_bq.b2);
  EXPECT_EQ(bq.a1, test_bq.a1);
  EXPECT_EQ(bq.a2, test_bq.a2);

  bq = biquad_new_set(BQ_ALLPASS, f_over, 0, db_gain);
  test_bq = {0, 0, 0, 0, 0, 0, 0, 0, 0};
  test_bq.b0 = 1;
  EXPECT_EQ(bq.b0, test_bq.b0);
  EXPECT_EQ(bq.b1, test_bq.b1);
  EXPECT_EQ(bq.b2, test_bq.b2);
  EXPECT_EQ(bq.a1, test_bq.a1);
  EXPECT_EQ(bq.a2, test_bq.a2);

  // check response to frew <= 0
  bq = biquad_new_set(BQ_LOWPASS, f_under, 0, db_gain);
  test_bq = {0, 0, 0, 0, 0, 0, 0, 0, 0};
  EXPECT_EQ(bq.b0, test_bq.b0);
  EXPECT_EQ(bq.b1, test_bq.b1);
  EXPECT_EQ(bq.b2, test_bq.b2);
  EXPECT_EQ(bq.a1, test_bq.a1);
  EXPECT_EQ(bq.a2, test_bq.a2);

  bq = biquad_new_set(BQ_HIGHPASS, f_under, 0, db_gain);
  test_bq = {0, 0, 0, 0, 0, 0, 0, 0, 0};
  test_bq.b0 = 1;
  EXPECT_EQ(bq.b0, test_bq.b0);
  EXPECT_EQ(bq.b1, test_bq.b1);
  EXPECT_EQ(bq.b2, test_bq.b2);
  EXPECT_EQ(bq.a1, test_bq.a1);
  EXPECT_EQ(bq.a2, test_bq.a2);

  bq = biquad_new_set(BQ_BANDPASS, f_under, 0, db_gain);
  test_bq = {0, 0, 0, 0, 0, 0, 0, 0, 0};
  EXPECT_EQ(bq.b0, test_bq.b0);
  EXPECT_EQ(bq.b1, test_bq.b1);
  EXPECT_EQ(bq.b2, test_bq.b2);
  EXPECT_EQ(bq.a1, test_bq.a1);
  EXPECT_EQ(bq.a2, test_bq.a2);

  bq = biquad_new_set(BQ_LOWSHELF, f_under, 0, db_gain);
  test_bq = {0, 0, 0, 0, 0, 0, 0, 0, 0};
  test_bq.b0 = 1;
  EXPECT_EQ(bq.b0, test_bq.b0);
  EXPECT_EQ(bq.b1, test_bq.b1);
  EXPECT_EQ(bq.b2, test_bq.b2);
  EXPECT_EQ(bq.a1, test_bq.a1);
  EXPECT_EQ(bq.a2, test_bq.a2);

  bq = biquad_new_set(BQ_HIGHSHELF, f_under, 0, db_gain);
  test_bq = {0, 0, 0, 0, 0, 0, 0, 0, 0};
  test_bq.b0 = A * A;
  EXPECT_EQ(bq.b0, test_bq.b0);
  EXPECT_EQ(bq.b1, test_bq.b1);
  EXPECT_EQ(bq.b2, test_bq.b2);
  EXPECT_EQ(bq.a1, test_bq.a1);
  EXPECT_EQ(bq.a2, test_bq.a2);

  bq = biquad_new_set(BQ_PEAKING, f_under, 0, db_gain);
  test_bq = {0, 0, 0, 0, 0, 0, 0, 0, 0};
  test_bq.b0 = 1;
  EXPECT_EQ(bq.b0, test_bq.b0);
  EXPECT_EQ(bq.b1, test_bq.b1);
  EXPECT_EQ(bq.b2, test_bq.b2);
  EXPECT_EQ(bq.a1, test_bq.a1);
  EXPECT_EQ(bq.a2, test_bq.a2);

  bq = biquad_new_set(BQ_NOTCH, f_under, 0, db_gain);
  test_bq = {0, 0, 0, 0, 0, 0, 0, 0, 0};
  test_bq.b0 = 1;
  EXPECT_EQ(bq.b0, test_bq.b0);
  EXPECT_EQ(bq.b1, test_bq.b1);
  EXPECT_EQ(bq.b2, test_bq.b2);
  EXPECT_EQ(bq.a1, test_bq.a1);
  EXPECT_EQ(bq.a2, test_bq.a2);

  bq = biquad_new_set(BQ_ALLPASS, f_under, 0, db_gain);
  test_bq = {0, 0, 0, 0, 0, 0, 0, 0, 0};
  test_bq.b0 = 1;
  EXPECT_EQ(bq.b0, test_bq.b0);
  EXPECT_EQ(bq.b1, test_bq.b1);
  EXPECT_EQ(bq.b2, test_bq.b2);
  EXPECT_EQ(bq.a1, test_bq.a1);
  EXPECT_EQ(bq.a2, test_bq.a2);
}

TEST(InvalidQTest, All) {
  struct biquad bq, test_bq;
  float f = 0.5;
  float Q = -0.1;
  double db_gain = 2;
  double A = pow(10.0, db_gain / 40);

  // check response to Q <= 0
  // Low and High pass filters scope Q making the test mute

  bq = biquad_new_set(BQ_BANDPASS, f, Q, db_gain);
  test_bq = {0, 0, 0, 0, 0, 0, 0, 0, 0};
  test_bq.b0 = 1;
  EXPECT_EQ(bq.b0, test_bq.b0);
  EXPECT_EQ(bq.b1, test_bq.b1);
  EXPECT_EQ(bq.b2, test_bq.b2);
  EXPECT_EQ(bq.a1, test_bq.a1);
  EXPECT_EQ(bq.a2, test_bq.a2);

  // Low and high shelf do not compute resonance

  bq = biquad_new_set(BQ_PEAKING, f, Q, db_gain);
  test_bq = {0, 0, 0, 0, 0, 0, 0, 0, 0};
  test_bq.b0 = A * A;
  EXPECT_EQ(bq.b0, test_bq.b0);
  EXPECT_EQ(bq.b1, test_bq.b1);
  EXPECT_EQ(bq.b2, test_bq.b2);
  EXPECT_EQ(bq.a1, test_bq.a1);
  EXPECT_EQ(bq.a2, test_bq.a2);

  bq = biquad_new_set(BQ_NOTCH, f, 0, db_gain);
  test_bq = {0, 0, 0, 0, 0, 0, 0, 0, 0};
  EXPECT_EQ(bq.b0, test_bq.b0);
  EXPECT_EQ(bq.b1, test_bq.b1);
  EXPECT_EQ(bq.b2, test_bq.b2);
  EXPECT_EQ(bq.a1, test_bq.a1);
  EXPECT_EQ(bq.a2, test_bq.a2);

  bq = biquad_new_set(BQ_ALLPASS, f, 0, db_gain);
  test_bq = {0, 0, 0, 0, 0, 0, 0, 0, 0};
  test_bq.b0 = -1;
  EXPECT_EQ(bq.b0, test_bq.b0);
  EXPECT_EQ(bq.b1, test_bq.b1);
  EXPECT_EQ(bq.b2, test_bq.b2);
  EXPECT_EQ(bq.a1, test_bq.a1);
  EXPECT_EQ(bq.a2, test_bq.a2);
}

}  //  namespace
