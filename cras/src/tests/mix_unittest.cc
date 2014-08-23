// Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stdio.h>
#include <gtest/gtest.h>

extern "C" {
#include "cras_shm.h"
#include "cras_mix.h"
#include "cras_types.h"

}

namespace {

static const size_t kBufferFrames = 8192;
static const size_t kNumChannels = 2;
static const size_t kNumSamples = kBufferFrames * kNumChannels;

class MixTestSuite : public testing::Test{
  protected:
    virtual void SetUp() {
      mix_buffer_ = (int16_t *)malloc(kBufferFrames * 4);
      src_buffer_ = static_cast<int16_t *>(
          calloc(1, kBufferFrames * 4 + sizeof(cras_audio_shm_area)));

      for (size_t i = 0; i < kBufferFrames * 2; i++) {
        src_buffer_[i] = i;
        mix_buffer_[i] = -i;
      }

      compare_buffer_ = (int16_t *)malloc(kBufferFrames * 4);
    }

    virtual void TearDown() {
      free(mix_buffer_);
      free(compare_buffer_);
      free(src_buffer_);
    }

  int16_t *mix_buffer_;
  int16_t *src_buffer_;
  int16_t *compare_buffer_;
};

TEST_F(MixTestSuite, MixFirst) {
  cras_mix_add(
      mix_buffer_, src_buffer_, kNumSamples, 0, 0, 1.0);
  EXPECT_EQ(0, memcmp(mix_buffer_, src_buffer_, kBufferFrames*4));
}

TEST_F(MixTestSuite, MixTwo) {
  cras_mix_add(
      mix_buffer_, src_buffer_, kNumSamples, 0, 0, 1.0);
  cras_mix_add(
      mix_buffer_, src_buffer_, kNumSamples, 1, 0, 1.0);

  for (size_t i = 0; i < kBufferFrames * 2; i++)
    compare_buffer_[i] = src_buffer_[i] * 2;
  EXPECT_EQ(0, memcmp(mix_buffer_, compare_buffer_, kBufferFrames*4));
}

TEST_F(MixTestSuite, MixFirstMuted) {
  cras_mix_add(
      mix_buffer_, src_buffer_, kNumSamples, 0, 1, 1.0);

  for (size_t i = 0; i < kBufferFrames * 2; i++)
    compare_buffer_[i] = 0;
  EXPECT_EQ(0, memcmp(mix_buffer_, compare_buffer_, kBufferFrames*4));
}

TEST_F(MixTestSuite, MixFirstZeroVolume) {
  cras_mix_add(
      mix_buffer_, src_buffer_, kNumSamples, 0, 0, 0.0);

  for (size_t i = 0; i < kBufferFrames * 2; i++)
    compare_buffer_[i] = 0;
  EXPECT_EQ(0, memcmp(mix_buffer_, compare_buffer_, kBufferFrames*4));
}

TEST_F(MixTestSuite, MixFirstHalfVolume) {
  cras_mix_add(
      mix_buffer_, src_buffer_, kNumSamples, 0, 0, 0.5);

  for (size_t i = 0; i < kBufferFrames * 2; i++)
    compare_buffer_[i] = src_buffer_[i] * 0.5;
  EXPECT_EQ(0, memcmp(mix_buffer_, compare_buffer_, kBufferFrames*4));
}

TEST_F(MixTestSuite, MixTwoSecondHalfVolume) {
  cras_mix_add(
      mix_buffer_, src_buffer_, kNumSamples, 0, 0, 1.0);
  cras_mix_add(
      mix_buffer_, src_buffer_, kNumSamples, 1, 0, 0.5);

  for (size_t i = 0; i < kBufferFrames * 2; i++)
    compare_buffer_[i] = src_buffer_[i] + (int16_t)(src_buffer_[i] * 0.5);
  EXPECT_EQ(0, memcmp(mix_buffer_, compare_buffer_, kBufferFrames*4));
}

/* Stubs */
extern "C" {

}  // extern "C"

}  //  namespace

int main(int argc, char **argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
