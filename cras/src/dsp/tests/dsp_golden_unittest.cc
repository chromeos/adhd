// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <cstddef>
#include <fcntl.h>
#include <gtest/gtest.h>
#include <stdio.h>
#include <vector>

#include "cras/common/check.h"
#include "cras/src/dsp/tests/raw.h"

#ifdef CRAS_DSP_RUST
#include "cras/src/dsp/rust/dsp.h"
#else
#include "cras/src/dsp/c/biquad.h"
#include "cras/src/dsp/c/crossover.h"
#include "cras/src/dsp/c/crossover2.h"
#include "cras/src/dsp/c/dcblock.h"
#include "cras/src/dsp/c/drc.h"
#include "cras/src/dsp/c/eq.h"
#include "cras/src/dsp/c/eq2.h"
#endif

#ifndef min
#define min(a, b)           \
  ({                        \
    __typeof__(a) _a = (a); \
    __typeof__(b) _b = (b); \
    _a < _b ? _a : _b;      \
  })
#endif

class GoldenTestdata {
 private:
  float* expected_output;
  size_t expected_output_size;

 public:
  float* input;
  size_t input_size;
  enum test_name {
    CROSSOVER,
    CROSSOVER2,
    DCBLOCK,
    DRC,
    EQ,
    EQ2,
  };

  GoldenTestdata(enum test_name test) {
    if (test == CROSSOVER) {
      expected_output = read_raw(
          "external/the_quick_brown_fox_golden_testdata/"
          "the-quick-brown-fox-crossover-out.raw",
          &expected_output_size);
    } else if (test == CROSSOVER2) {
      expected_output = read_raw(
          "external/the_quick_brown_fox_golden_testdata/"
          "the-quick-brown-fox-crossover2-out.raw",
          &expected_output_size);
    } else if (test == DCBLOCK) {
      expected_output = read_raw(
          "external/the_quick_brown_fox_golden_testdata/"
          "the-quick-brown-fox-dcblock-out.raw",
          &expected_output_size);
    } else if (test == DRC) {
      expected_output = read_raw(
          "external/the_quick_brown_fox_golden_testdata/"
          "the-quick-brown-fox-drc-out.raw",
          &expected_output_size);
    } else if (test == EQ) {
      expected_output = read_raw(
          "external/the_quick_brown_fox_golden_testdata/"
          "the-quick-brown-fox-eq-out.raw",
          &expected_output_size);
    } else if (test == EQ2) {
      expected_output = read_raw(
          "external/the_quick_brown_fox_golden_testdata/"
          "the-quick-brown-fox-eq2-out.raw",
          &expected_output_size);
    } else {
      CRAS_CHECK(false && "invalid test parameter");
    }
    input = read_raw(
        "external/the_quick_brown_fox_golden_testdata/the-quick-brown-fox.raw",
        &input_size);
  }

  ~GoldenTestdata() {
    free(input);
    free(expected_output);
  }

  void compare_output(float* output, size_t output_size) {
    ASSERT_EQ(output_size, expected_output_size);

    size_t n = output_size * 2;
    for (int i = 0; i < n; i++) {
      EXPECT_NEAR(output[i], expected_output[i], 0.001);
    }
  }
};

TEST(DspTestSuite, Crossover) {
  GoldenTestdata golden_testdata(GoldenTestdata::CROSSOVER);

  float* input = golden_testdata.input;
  size_t input_size = golden_testdata.input_size;

  std::vector<std::vector<float>> data(2, std::vector<float>(input_size * 2));

  double NQ = 44100 / 2;
  struct crossover xo;
  crossover_init(&xo, 400 / NQ, 4000 / NQ);
  for (int start = 0; start < input_size; start += 2048) {
    crossover_process(&xo, min(2048, input_size - start), input + start,
                      data[0].data() + start, data[1].data() + start);
  }

  crossover_init(&xo, 400 / NQ, 4000 / NQ);
  for (int start = 0; start < input_size; start += 2048) {
    crossover_process(&xo, min(2048, input_size - start),
                      input + input_size + start,
                      data[0].data() + input_size + start,
                      data[1].data() + input_size + start);
  }

  size_t n = input_size * 2;
  for (int i = 0; i < n; i++) {
    input[i] += data[0][i] + data[1][i];
  }

  golden_testdata.compare_output(input, input_size);
}

TEST(DspTestSuite, Crossover2) {
  GoldenTestdata golden_testdata(GoldenTestdata::CROSSOVER2);

  float* input = golden_testdata.input;
  size_t input_size = golden_testdata.input_size;

  std::vector<std::vector<float>> data(2, std::vector<float>(input_size * 2));

  double NQ = 44100 / 2;
  struct crossover2 xo2;
  crossover2_init(&xo2, 400 / NQ, 4000 / NQ);
  for (int start = 0; start < input_size; start += 2048) {
    crossover2_process(&xo2, min(2048, input_size - start), input + start,
                       input + input_size + start, data[0].data() + start,
                       data[0].data() + input_size + start,
                       data[1].data() + start,
                       data[1].data() + input_size + start);
  }

  size_t n = input_size * 2;
  for (int i = 0; i < n; i++) {
    input[i] += data[0][i] + data[1][i];
  }

  golden_testdata.compare_output(input, input_size);
}

TEST(DspTestSuite, DCBlock) {
  GoldenTestdata golden_testdata(GoldenTestdata::DCBLOCK);

  float* input = golden_testdata.input;
  size_t input_size = golden_testdata.input_size;

  struct dcblock* dcblockl = dcblock_new();
  struct dcblock* dcblockr = dcblock_new();

  ASSERT_TRUE(dcblockl);
  ASSERT_TRUE(dcblockr);

  dcblock_set_config(dcblockl, 0.995, 48000);
  dcblock_set_config(dcblockr, 0.995, 48000);

  for (int start = 0; start < input_size; start += 128) {
    dcblock_process(dcblockl, input + start, min(128, input_size - start));
  }
  for (int start = 0; start < input_size; start += 128) {
    dcblock_process(dcblockr, input + input_size + start,
                    min(128, input_size - start));
  }

  golden_testdata.compare_output(input, input_size);

  dcblock_free(dcblockl);
  dcblock_free(dcblockr);
}

TEST(DspTestSuite, DRC) {
  GoldenTestdata golden_testdata(GoldenTestdata::DRC);

  float* input = golden_testdata.input;
  size_t input_size = golden_testdata.input_size;

  struct drc* drc = drc_new(44100);

#ifdef CRAS_DSP_RUST
  drc_set_emphasis_disabled(drc, 0);
#else
  drc->emphasis_disabled = 0;
#endif
  drc_set_param(drc, 0, PARAM_CROSSOVER_LOWER_FREQ, 0);
  drc_set_param(drc, 0, PARAM_ENABLED, 1);
  drc_set_param(drc, 0, PARAM_THRESHOLD, -29);
  drc_set_param(drc, 0, PARAM_KNEE, 3);
  drc_set_param(drc, 0, PARAM_RATIO, 6.677);
  drc_set_param(drc, 0, PARAM_ATTACK, 0.02);
  drc_set_param(drc, 0, PARAM_RELEASE, 0.2);
  drc_set_param(drc, 0, PARAM_POST_GAIN, -7);

  double NQ = 44100 / 2;  // nyquist frequency
  drc_set_param(drc, 1, PARAM_CROSSOVER_LOWER_FREQ, 200 / NQ);
  drc_set_param(drc, 1, PARAM_ENABLED, 1);
  drc_set_param(drc, 1, PARAM_THRESHOLD, -32);
  drc_set_param(drc, 1, PARAM_KNEE, 23);
  drc_set_param(drc, 1, PARAM_RATIO, 12);
  drc_set_param(drc, 1, PARAM_ATTACK, 0.02);
  drc_set_param(drc, 1, PARAM_RELEASE, 0.2);
  drc_set_param(drc, 1, PARAM_POST_GAIN, 0.7);

  drc_set_param(drc, 2, PARAM_CROSSOVER_LOWER_FREQ, 1200 / NQ);
  drc_set_param(drc, 2, PARAM_ENABLED, 1);
  drc_set_param(drc, 2, PARAM_THRESHOLD, -24);
  drc_set_param(drc, 2, PARAM_KNEE, 30);
  drc_set_param(drc, 2, PARAM_RATIO, 1);
  drc_set_param(drc, 2, PARAM_ATTACK, 0.001);
  drc_set_param(drc, 2, PARAM_RELEASE, 1);
  drc_set_param(drc, 2, PARAM_POST_GAIN, 0);

  drc_init(drc);

  for (int start = 0; start < input_size;) {
    float* data[2];
    data[0] = input + start;
    data[1] = input + start + input_size;
    int chunk = min(DRC_PROCESS_MAX_FRAMES, input_size - start);
    drc_process(drc, data, chunk);
    start += chunk;
  }

  golden_testdata.compare_output(input, input_size);

  drc_free(drc);
}

TEST(DspTestSuite, EQ) {
  GoldenTestdata golden_testdata(GoldenTestdata::EQ);

  float* input = golden_testdata.input;
  size_t input_size = golden_testdata.input_size;

  struct eq* eq;

  // Set some data to 0 to test for denormals.
  for (int i = input_size / 10; i < input_size; i++) {
    input[i] = 0.0;
  }

  // Left eq chain
  eq = eq_new();
  double NQ = 44100 / 2;  // nyquist frequency
  eq_append_biquad(eq, BQ_PEAKING, 380 / NQ, 3, -10);
  eq_append_biquad(eq, BQ_PEAKING, 720 / NQ, 3, -12);
  eq_append_biquad(eq, BQ_PEAKING, 1705 / NQ, 3, -8);
  eq_append_biquad(eq, BQ_HIGHPASS, 218 / NQ, 0.7, -10.2);
  eq_append_biquad(eq, BQ_PEAKING, 580 / NQ, 6, -8);
  eq_append_biquad(eq, BQ_HIGHSHELF, 8000 / NQ, 3, 2);
  for (int start = 0; start < input_size; start += 2048) {
    eq_process(eq, input + start, min(2048, input_size - start));
  }
  eq_free(eq);

  // Right eq chain
  eq = eq_new();
  eq_append_biquad(eq, BQ_PEAKING, 450 / NQ, 3, -12);
  eq_append_biquad(eq, BQ_PEAKING, 721 / NQ, 3, -12);
  eq_append_biquad(eq, BQ_PEAKING, 1800 / NQ, 8, -10.2);
  eq_append_biquad(eq, BQ_PEAKING, 580 / NQ, 6, -8);
  eq_append_biquad(eq, BQ_HIGHPASS, 250 / NQ, 0.6578, 0);
  eq_append_biquad(eq, BQ_HIGHSHELF, 8000 / NQ, 0, 2);
  for (int start = 0; start < input_size; start += 2048) {
    eq_process(eq, input + input_size + start, min(2048, input_size - start));
  }
  eq_free(eq);

  golden_testdata.compare_output(input, input_size);
}

TEST(DspTestSuite, EQ2) {
  GoldenTestdata golden_testdata(GoldenTestdata::EQ2);

  float* input = golden_testdata.input;
  size_t input_size = golden_testdata.input_size;

  // Set some data to 0 to test for denormals.
  for (int i = input_size / 10; i < input_size; i++) {
    input[i] = 0.0;
  }

  // eq chain
  struct eq2* eq2 = eq2_new();
  double NQ = 44100 / 2;  // nyquist frequency
  eq2_append_biquad(eq2, 0, BQ_PEAKING, 380 / NQ, 3, -10);
  eq2_append_biquad(eq2, 0, BQ_PEAKING, 720 / NQ, 3, -12);
  eq2_append_biquad(eq2, 0, BQ_PEAKING, 1705 / NQ, 3, -8);
  eq2_append_biquad(eq2, 0, BQ_HIGHPASS, 218 / NQ, 0.7, -10.2);
  eq2_append_biquad(eq2, 0, BQ_PEAKING, 580 / NQ, 6, -8);
  eq2_append_biquad(eq2, 0, BQ_HIGHSHELF, 8000 / NQ, 3, 2);
  eq2_append_biquad(eq2, 1, BQ_PEAKING, 450 / NQ, 3, -12);
  eq2_append_biquad(eq2, 1, BQ_PEAKING, 721 / NQ, 3, -12);
  eq2_append_biquad(eq2, 1, BQ_PEAKING, 1800 / NQ, 8, -10.2);
  eq2_append_biquad(eq2, 1, BQ_PEAKING, 580 / NQ, 6, -8);
  eq2_append_biquad(eq2, 1, BQ_HIGHPASS, 250 / NQ, 0.6578, 0);
  eq2_append_biquad(eq2, 1, BQ_HIGHSHELF, 8000 / NQ, 0, 2);
  for (int start = 0; start < input_size; start += 2048) {
    eq2_process(eq2, input + start, input + input_size + start,
                min(2048, input_size - start));
  }
  eq2_free(eq2);

  golden_testdata.compare_output(input, input_size);
}
