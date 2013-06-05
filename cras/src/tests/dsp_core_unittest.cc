// Copyright (c) 2013 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <gtest/gtest.h>
#include <math.h>
#include "dsp_util.h"
#include "eq.h"

namespace {

/* Adds amplitude * sin(pi*freq*i + offset) to the data array. */
static void add_sine(float *data, size_t len, float freq, float offset,
                     float amplitude)
{
  for (size_t i = 0; i < len; i++)
    data[i] += amplitude * sinf((float)M_PI*freq*i + offset);
}

/* Calculates the magnitude at normalized frequency f. The output is
 * the result of DFT, multiplied by 2/len. */
static float magnitude_at(float *data, size_t len, float f)
{
  double re = 0, im = 0;
  f *= (float)M_PI;
  for (size_t i = 0; i < len; i++) {
    re += data[i] * cos(i * f);
    im += data[i] * sin(i * f);
  }
  return sqrt(re * re + im * im) * (2.0 / len);
}

TEST(EqTest, All) {
  struct eq *eq;
  size_t len = 44100;
  float NQ = len / 2;
  float f_low = 10 / NQ;
  float f_mid = 100 / NQ;
  float f_high = 1000 / NQ;
  float *data = (float *)malloc(sizeof(float) * len);

  dsp_enable_flush_denormal_to_zero();
  /* low pass */
  memset(data, 0, sizeof(float) * len);
  add_sine(data, len, f_low, 0, 1);  // 10Hz sine, magnitude = 1
  EXPECT_FLOAT_EQ(1, magnitude_at(data, len, f_low));
  add_sine(data, len, f_high, 0, 1);  // 1000Hz sine, magnitude = 1
  EXPECT_FLOAT_EQ(1, magnitude_at(data, len, f_low));
  EXPECT_FLOAT_EQ(1, magnitude_at(data, len, f_high));

  eq = eq_new();
  EXPECT_EQ(0, eq_append_biquad(eq, BQ_LOWPASS, f_mid, 0, 0));
  eq_process(eq, data, len);
  EXPECT_NEAR(1, magnitude_at(data, len, f_low), 0.01);
  EXPECT_NEAR(0, magnitude_at(data, len, f_high), 0.01);
  eq_free(eq);

  /* high pass */
  memset(data, 0, sizeof(float) * len);
  add_sine(data, len, f_low, 0, 1);
  add_sine(data, len, f_high, 0, 1);

  eq = eq_new();
  EXPECT_EQ(0, eq_append_biquad(eq, BQ_HIGHPASS, f_mid, 0, 0));
  eq_process(eq, data, len);
  EXPECT_NEAR(0, magnitude_at(data, len, f_low), 0.01);
  EXPECT_NEAR(1, magnitude_at(data, len, f_high), 0.01);
  eq_free(eq);

  /* peaking */
  memset(data, 0, sizeof(float) * len);
  add_sine(data, len, f_low, 0, 1);
  add_sine(data, len, f_high, 0, 1);

  eq = eq_new();
  EXPECT_EQ(0, eq_append_biquad(eq, BQ_PEAKING, f_high, 5, 6)); // Q=5, 6dB gain
  eq_process(eq, data, len);
  EXPECT_NEAR(1, magnitude_at(data, len, f_low), 0.01);
  EXPECT_NEAR(2, magnitude_at(data, len, f_high), 0.01);
  eq_free(eq);

  free(data);

  /* Too many biquads */
  eq = eq_new();
  for (int i = 0; i < MAX_BIQUADS_PER_EQ; i++) {
    EXPECT_EQ(0, eq_append_biquad(eq, BQ_PEAKING, f_high, 5, 6));
  }
  EXPECT_EQ(-1, eq_append_biquad(eq, BQ_PEAKING, f_high, 5, 6));
  eq_free(eq);
}

}  //  namespace

int main(int argc, char **argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
