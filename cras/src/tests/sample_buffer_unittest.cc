// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <gtest/gtest.h>

#include "cras/src/common/byte_buffer.h"
#include "cras/src/common/sample_buffer.h"

namespace {

struct SampleBufferTestParam {
  unsigned int sample_size;
  unsigned int num_bytes;
};

class SampleBufferTestSuite
    : public testing::TestWithParam<SampleBufferTestParam> {};

TEST_P(SampleBufferTestSuite, TestInitCleanUpBuffer) {
  const auto& param = SampleBufferTestSuite::GetParam();
  const size_t num_samples = 3;
  struct sample_buffer buf = {};

  EXPECT_EQ(sample_buffer_init(num_samples, param.sample_size, &buf), 0);
  EXPECT_EQ(sample_buf_available(&buf), num_samples);

  sample_buffer_cleanup(&buf);
}

TEST_P(SampleBufferTestSuite, TestWeakRef) {
  const auto& param = SampleBufferTestSuite::GetParam();
  const size_t num_samples = 3;
  struct byte_buffer* byte_buf =
      byte_buffer_create(param.sample_size * num_samples);

  sample_buffer_weak_ref(byte_buf, param.sample_size);

  // After the sample buf disappeared, byte_buf is still available.
  buf_write_pointer(byte_buf)[0] = 0;
  byte_buffer_destroy(&byte_buf);
}

TEST_P(SampleBufferTestSuite, TestWeakRefExternalBuffer) {
  const auto& param = SampleBufferTestSuite::GetParam();
  struct byte_buffer* buf = byte_buffer_create(param.num_bytes);
  struct sample_buffer sample_buf =
      sample_buffer_weak_ref(buf, param.sample_size);

  EXPECT_EQ(sample_buf_readable(&sample_buf), 0);
  EXPECT_EQ(sample_buf_queued(&sample_buf), 0);
  EXPECT_EQ(sample_buf_writable(&sample_buf),
            param.num_bytes / param.sample_size);
  EXPECT_EQ(sample_buf_available(&sample_buf),
            param.num_bytes / param.sample_size);
  EXPECT_EQ(sample_buf_write_pointer(&sample_buf), buf_write_pointer(buf));
  EXPECT_EQ(sample_buf_read_pointer(&sample_buf), buf_read_pointer(buf));

  unsigned int num_writable_sample = 0;
  EXPECT_EQ(sample_buf_write_pointer_size(&sample_buf, &num_writable_sample),
            buf_write_pointer(buf));
  EXPECT_EQ(num_writable_sample, param.num_bytes / param.sample_size);

  unsigned int num_readable_sample = 0;
  EXPECT_EQ(sample_buf_read_pointer_size(&sample_buf, &num_readable_sample),
            buf_read_pointer(buf));
  EXPECT_EQ(num_readable_sample, 0);

  byte_buffer_destroy(&buf);
}

TEST_P(SampleBufferTestSuite, TestWriteReadExternalBuffer) {
  const auto& param = SampleBufferTestSuite::GetParam();
  struct byte_buffer* buf = byte_buffer_create(param.num_bytes);
  struct sample_buffer sample_buf =
      sample_buffer_weak_ref(buf, param.sample_size);

  {  // writes sample(s)
    sample_buf_increment_write(&sample_buf, 1);

    EXPECT_EQ(sample_buf_queued(&sample_buf), 1);
    EXPECT_EQ(buf_queued(buf), 1 * param.sample_size);
    EXPECT_EQ(sample_buf_readable(&sample_buf), 1);
    EXPECT_EQ(buf_readable(buf), 1 * param.sample_size);

    EXPECT_EQ(sample_buf_available(&sample_buf),
              buf_available(buf) / param.sample_size);
    EXPECT_EQ(sample_buf_writable(&sample_buf),
              buf_writable(buf) / param.sample_size);
  }

  {  // reads sample(s)
    sample_buf_increment_read(&sample_buf, 1);

    EXPECT_EQ(sample_buf_queued(&sample_buf), 0);
    EXPECT_EQ(buf_queued(buf), 0);
    EXPECT_EQ(sample_buf_readable(&sample_buf), 0);
    EXPECT_EQ(buf_readable(buf), 0);

    EXPECT_EQ(sample_buf_available(&sample_buf),
              buf_available(buf) / param.sample_size);
    EXPECT_EQ(sample_buf_writable(&sample_buf),
              buf_writable(buf) / param.sample_size);
  }

  byte_buffer_destroy(&buf);
}

TEST_P(SampleBufferTestSuite, TestSampleBufFullWithZeroReadIndex) {
  const auto& param = SampleBufferTestSuite::GetParam();
  struct sample_buffer buf = {};
  const size_t num_samples = 3;
  EXPECT_EQ(sample_buffer_init(num_samples, param.sample_size, &buf), 0);

  EXPECT_EQ(sample_buf_full_with_zero_read_index(&buf), 0);
  sample_buf_increment_write(&buf, num_samples);
  EXPECT_EQ(sample_buf_full_with_zero_read_index(&buf), 1);
  sample_buf_increment_read(&buf, num_samples);
  EXPECT_EQ(sample_buf_full_with_zero_read_index(&buf), 0);

  sample_buffer_cleanup(&buf);
}

INSTANTIATE_TEST_SUITE_P(
    TestWithParams,
    SampleBufferTestSuite,
    testing::Values(SampleBufferTestParam{.sample_size = 1, .num_bytes = 2},
                    SampleBufferTestParam{.sample_size = 2, .num_bytes = 2},
                    SampleBufferTestParam{.sample_size = 2, .num_bytes = 4},
                    SampleBufferTestParam{.sample_size = 3, .num_bytes = 3},
                    SampleBufferTestParam{.sample_size = 4, .num_bytes = 4}));

class SampleBufferTestCheckSuite
    : public testing::TestWithParam<SampleBufferTestParam> {};

TEST_P(SampleBufferTestCheckSuite, TestCheckFalse) {
  const auto& param = SampleBufferTestSuite::GetParam();
  struct byte_buffer* buf = byte_buffer_create(param.num_bytes);

  ASSERT_DEATH(sample_buffer_weak_ref(buf, param.sample_size),
               "sample_buffer_validate_byte_buffer failed.");

  byte_buffer_destroy(&buf);
}

INSTANTIATE_TEST_SUITE_P(
    TestWithParams,
    SampleBufferTestCheckSuite,
    testing::Values(SampleBufferTestParam{.sample_size = 2, .num_bytes = 3},
                    SampleBufferTestParam{.sample_size = 3, .num_bytes = 4},
                    SampleBufferTestParam{.sample_size = 4, .num_bytes = 5}));

}  // namespace
