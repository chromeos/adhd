/* Copyright 2022 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <cstdio>
#include <gtest/gtest.h>
#include <vector>

#include "cras/src/common/sample_buffer.h"

extern "C" {
#include "cras/src/dsp/am.h"
#include "cras/src/server/cras_sr.h"
}

namespace {

// Helper functions for testing.

template <typename T>
inline void Fill(struct byte_buffer* buf, T value, size_t num_samples) {
  for (int i = 0; i < 2; ++i) {
    unsigned int num_writable = buf_writable(buf) / sizeof(T);
    T* ptr = reinterpret_cast<T*>(buf_write_pointer(buf));
    size_t num_written = MIN(num_writable, num_samples);
    for (int j = 0; j < num_written; ++j) {
      ptr[j] = value;
    }
    buf_increment_write(buf, sizeof(T) * num_written);
    num_samples -= num_written;
    if (num_samples == 0) {
      break;
    }
  }
  assert(num_samples == 0);
}

template <typename T>
inline void FillZeros(struct byte_buffer* buf, size_t num_zeros) {
  Fill<T>(buf, 0, num_zeros);
}

template <typename T>
testing::AssertionResult BufNumSamplesEQ(struct byte_buffer* buf,
                                         size_t expected) {
  struct sample_buffer sample_buf = sample_buffer_weak_ref(buf, sizeof(T));
  const unsigned int num_readable = sample_buf_readable(&sample_buf);
  if (num_readable == expected) {
    return testing::AssertionSuccess();
  } else {
    return testing::AssertionFailure()
           << "Num readable samples (" << num_readable << ") "
           << "!= expected (" << expected << ").";
  }
}

template <typename T>
void BufVecEQ(struct byte_buffer* buf, const std::vector<T>& expected_values) {
  const auto expected_num_outputs = (expected_values).size();
  ASSERT_TRUE(BufNumSamplesEQ<int16_t>(buf, expected_num_outputs));
  T* buf_ptr = reinterpret_cast<T*>(buf_read_pointer(buf));
  for (int i = 0; i < expected_num_outputs; ++i) {
    ASSERT_EQ(buf_ptr[i], (expected_values)[i]) << "index: " << i;
  }
  buf_increment_read(buf, expected_num_outputs * sizeof(int16_t));
}

template <typename T>
void BufValEQ(struct byte_buffer* buf,
              const size_t num_elements,
              const T expected_value) {
  const std::vector<T> expected_values(num_elements, expected_value);
  BufVecEQ(buf, expected_values);
}

// Tests

class BtSrTestSuite : public testing::Test {
 protected:
  void SetUp() override {
    input_buf = byte_buffer_create(sizeof(int16_t) * 160 * 2);
    buf_reset(input_buf);
    output_buf = byte_buffer_create(sizeof(int16_t) * 480 * 2);
    buf_reset(output_buf);
    sr = cras_sr_create({.num_frames_per_run = 480,
                         .num_channels = 1,
                         .input_sample_rate = 8000,
                         .output_sample_rate = 24000},
                        buf_writable(input_buf));
  }

  void TearDown() override {
    byte_buffer_destroy(&input_buf);
    byte_buffer_destroy(&output_buf);
    cras_sr_destroy(sr);
  }

  struct cras_sr* sr;
  struct byte_buffer* input_buf;
  struct byte_buffer* output_buf;
};

TEST_F(BtSrTestSuite, HasPaddedZeros) {
  {
    SCOPED_TRACE("Expects consuming 30 samples and producing 90 padded zeros.");
    Fill<int16_t>(input_buf, 1, 30);
    cras_sr_process(sr, input_buf, output_buf);
    ASSERT_TRUE(BufNumSamplesEQ<int16_t>(input_buf, 0));
    BufValEQ<int16_t>(output_buf, 90, 0);
  }

  {
    SCOPED_TRACE(
        "Expects consuming 130 samples and producing 390 padded zeros.");
    Fill<int16_t>(input_buf, 1, 130);
    cras_sr_process(sr, input_buf, output_buf);
    ASSERT_TRUE(BufNumSamplesEQ<int16_t>(input_buf, 0));
    BufValEQ<int16_t>(output_buf, 390, 0);
  }

  {
    SCOPED_TRACE(
        "Expects consuming 160 samples and producing 480 processed values(1).");
    Fill<int16_t>(input_buf, 1, 160);
    cras_sr_process(sr, input_buf, output_buf);
    ASSERT_TRUE(BufNumSamplesEQ<int16_t>(input_buf, 0));
    BufValEQ<int16_t>(output_buf, 480, 1);
  }

  {
    SCOPED_TRACE(
        "Expects consuming 160 samples and producing 480 processed values(2).");
    Fill<int16_t>(input_buf, 1, 160);
    cras_sr_process(sr, input_buf, output_buf);
    ASSERT_TRUE(BufNumSamplesEQ<int16_t>(input_buf, 0));
    BufValEQ<int16_t>(output_buf, 480, 2);
  }
}

TEST_F(BtSrTestSuite, NumOutputsMoreThanNumFramesPerRun) {
  Fill<int16_t>(input_buf, 1, 170);
  std::vector<int16_t> expected;
  expected.resize(510);
  std::fill(expected.begin() + 480, expected.begin() + 510, 1);

  auto num_read_bytes = cras_sr_process(sr, input_buf, output_buf);

  {
    SCOPED_TRACE(
        "Expects consuming 170 samples and producing 480 padded zeros and 30 "
        "processed values(1).");
    EXPECT_EQ(num_read_bytes, 170 * sizeof(int16_t));
    ASSERT_TRUE(BufNumSamplesEQ<int16_t>(input_buf, 0));
    BufVecEQ<int16_t>(output_buf, expected);
  }
}

TEST_F(BtSrTestSuite, CachedInInternalBuffer) {
  {  // 1. output buf full, internal buf empty
    Fill<int16_t>(input_buf, 1, 320);
    FillZeros<int16_t>(output_buf, 960);

    auto num_read_bytes = cras_sr_process(sr, input_buf, output_buf);

    SCOPED_TRACE("Expects consuming 320 samples and producing 0 samples.");
    EXPECT_EQ(num_read_bytes, 320 * sizeof(int16_t));
    EXPECT_TRUE(BufNumSamplesEQ<int16_t>(input_buf, 0));
    BufValEQ<int16_t>(output_buf, 960, 0);
  }

  {  // 2. output buf full, internal buf full
    Fill<int16_t>(input_buf, 1, 10);
    FillZeros<int16_t>(output_buf, 960);

    auto num_read_bytes = cras_sr_process(sr, input_buf, output_buf);

    SCOPED_TRACE("Expects consuming 0 samples and producing 0 samples.");
    EXPECT_EQ(num_read_bytes, 0);
    EXPECT_TRUE(BufNumSamplesEQ<int16_t>(input_buf, 10));
    BufValEQ<int16_t>(output_buf, 960, 0);
  }

  {  // 3. internal buf full, output buf empty
    auto num_read_bytes = cras_sr_process(sr, input_buf, output_buf);

    SCOPED_TRACE(
        "Expects consuming 0 samples and producing 480 padded zeros and 480 "
        "processed values(1).");
    EXPECT_EQ(num_read_bytes, 10 * sizeof(int16_t));
    std::vector<int16_t> expected(960, 0);
    std::fill(expected.begin() + 480, expected.begin() + 960, 1);
    BufVecEQ(output_buf, expected);
  }

  {  // 4. flush
    auto num_read_bytes = cras_sr_process(sr, input_buf, output_buf);

    SCOPED_TRACE(
        "Expects consuming 0 samples and producing 30 processed values(2).");
    EXPECT_EQ(num_read_bytes, 0);
    std::vector<int16_t> expected(30, 2);
    BufVecEQ(output_buf, expected);
  }
}

TEST_F(BtSrTestSuite, OutputBufReachEnd) {
  Fill<int16_t>(input_buf, 1, 320);
  // Moves the write / read pointer to the mid of the buffer.
  FillZeros<int16_t>(output_buf, 480);
  buf_increment_read(output_buf, 480 * sizeof(int16_t));

  {  // 320 inputs will result in 960 outputs.
    SCOPED_TRACE("Expects consuming 320 samples.");
    auto num_read_bytes = cras_sr_process(sr, input_buf, output_buf);
    ASSERT_EQ(num_read_bytes, 320 * sizeof(int16_t));
  }

  {  // The 480 padded zeros from the mid of the buffer to the end.
    SCOPED_TRACE("Expects 480 padded zeros.");
    auto num_output = buf_readable(output_buf) / sizeof(int16_t);
    EXPECT_EQ(num_output, 480);
    BufValEQ<int16_t>(output_buf, 480, 0);
  }

  {  // The 480 processed values from the start of the buffer to the mid.
    SCOPED_TRACE("Expects 480 processed values(1).");
    auto num_output = buf_readable(output_buf) / sizeof(int16_t);
    EXPECT_EQ(num_output, 480);
    BufValEQ<int16_t>(output_buf, 480, 1);
  }
}

TEST_F(BtSrTestSuite, FramesRatio) {
  EXPECT_EQ(cras_sr_get_frames_ratio(sr), 3.);
}

TEST_F(BtSrTestSuite, NumFramesPerRun) {
  EXPECT_EQ(cras_sr_get_num_frames_per_run(sr), 480);
}

}  // namespace
