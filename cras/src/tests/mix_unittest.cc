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

class MixTestSuiteS16_LE : public testing::Test{
  protected:
    virtual void SetUp() {
      fmt_ = SND_PCM_FORMAT_S16_LE;
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
  snd_pcm_format_t fmt_;
};

TEST_F(MixTestSuiteS16_LE, MixFirst) {
  cras_mix_add(fmt_, (uint8_t *)mix_buffer_, (uint8_t *)src_buffer_,
                      kNumSamples, 0, 0, 1.0);
  EXPECT_EQ(0, memcmp(mix_buffer_, src_buffer_, kBufferFrames*4));
}

TEST_F(MixTestSuiteS16_LE, MixTwo) {
  cras_mix_add(fmt_, (uint8_t *)mix_buffer_, (uint8_t *)src_buffer_,
               kNumSamples, 0, 0, 1.0);
  cras_mix_add(fmt_, (uint8_t *)mix_buffer_, (uint8_t *)src_buffer_,
               kNumSamples, 1, 0, 1.0);

  for (size_t i = 0; i < kBufferFrames * 2; i++)
    compare_buffer_[i] = src_buffer_[i] * 2;
  EXPECT_EQ(0, memcmp(mix_buffer_, compare_buffer_, kBufferFrames*4));
}

TEST_F(MixTestSuiteS16_LE, MixTwoClip) {
  cras_mix_add(fmt_, (uint8_t *)mix_buffer_, (uint8_t *)src_buffer_,
               kNumSamples, 0, 0, 1.0);
  for (size_t i = 0; i < kBufferFrames * 2; i++)
    src_buffer_[i] = INT16_MAX;
  cras_mix_add(fmt_, (uint8_t *)mix_buffer_, (uint8_t *)src_buffer_,
               kNumSamples, 1, 0, 1.0);

  for (size_t i = 0; i < kBufferFrames * 2; i++)
    compare_buffer_[i] = INT16_MAX;
  EXPECT_EQ(0, memcmp(mix_buffer_, compare_buffer_, kBufferFrames*4));
}

TEST_F(MixTestSuiteS16_LE, MixFirstMuted) {
  cras_mix_add(fmt_, (uint8_t *)mix_buffer_, (uint8_t *)src_buffer_,
               kNumSamples, 0, 1, 1.0);

  for (size_t i = 0; i < kBufferFrames * 2; i++)
    compare_buffer_[i] = 0;
  EXPECT_EQ(0, memcmp(mix_buffer_, compare_buffer_, kBufferFrames*4));
}

TEST_F(MixTestSuiteS16_LE, MixFirstZeroVolume) {
  cras_mix_add(fmt_, (uint8_t *)mix_buffer_, (uint8_t *)src_buffer_,
                      kNumSamples, 0, 0, 0.0);

  for (size_t i = 0; i < kBufferFrames * 2; i++)
    compare_buffer_[i] = 0;
  EXPECT_EQ(0, memcmp(mix_buffer_, compare_buffer_, kBufferFrames*4));
}

TEST_F(MixTestSuiteS16_LE, MixFirstHalfVolume) {
  cras_mix_add(fmt_, (uint8_t *)mix_buffer_, (uint8_t *)src_buffer_,
                      kNumSamples, 0, 0, 0.5);

  for (size_t i = 0; i < kBufferFrames * 2; i++)
    compare_buffer_[i] = src_buffer_[i] * 0.5;
  EXPECT_EQ(0, memcmp(mix_buffer_, compare_buffer_, kBufferFrames*4));
}

TEST_F(MixTestSuiteS16_LE, MixTwoSecondHalfVolume) {
  cras_mix_add(fmt_, (uint8_t *)mix_buffer_, (uint8_t *)src_buffer_,
               kNumSamples, 0, 0, 1.0);
  cras_mix_add(fmt_, (uint8_t *)mix_buffer_, (uint8_t *)src_buffer_,
               kNumSamples, 1, 0, 0.5);

  for (size_t i = 0; i < kBufferFrames * 2; i++)
    compare_buffer_[i] = src_buffer_[i] + (int16_t)(src_buffer_[i] * 0.5);
  EXPECT_EQ(0, memcmp(mix_buffer_, compare_buffer_, kBufferFrames*4));
}

TEST_F(MixTestSuiteS16_LE, ScaleFullVolume) {
  memcpy(compare_buffer_, src_buffer_, kBufferFrames * 4);
  cras_scale_buffer(fmt_, (uint8_t *)mix_buffer_, kNumSamples, 0.999999999);

  EXPECT_EQ(0, memcmp(compare_buffer_, src_buffer_, kBufferFrames * 4));
}

TEST_F(MixTestSuiteS16_LE, ScaleMinVolume) {
  memset(compare_buffer_, 0, kBufferFrames * 4);
  cras_scale_buffer(fmt_, (uint8_t *)src_buffer_, kNumSamples, 0.0000000001);

  EXPECT_EQ(0, memcmp(compare_buffer_, src_buffer_, kBufferFrames * 4));
}

TEST_F(MixTestSuiteS16_LE, ScaleHalfVolume) {
  for (size_t i = 0; i < kBufferFrames * 2; i++)
    compare_buffer_[i] = src_buffer_[i] * 0.5;
  cras_scale_buffer(fmt_, (uint8_t *)src_buffer_, kNumSamples, 0.5);

  EXPECT_EQ(0, memcmp(compare_buffer_, src_buffer_, kBufferFrames * 4));
}

class MixTestSuiteS24_LE : public testing::Test{
  protected:
    virtual void SetUp() {
      fmt_ = SND_PCM_FORMAT_S24_LE;
      fr_bytes_ = 4 * kNumChannels;
      mix_buffer_ = (int32_t *)malloc(kBufferFrames * fr_bytes_);
      src_buffer_ = static_cast<int32_t *>(
          calloc(1, kBufferFrames * fr_bytes_ + sizeof(cras_audio_shm_area)));

      for (size_t i = 0; i < kBufferFrames * 2; i++) {
        src_buffer_[i] = i;
        mix_buffer_[i] = -i;
      }

      compare_buffer_ = (int32_t *)malloc(kBufferFrames * fr_bytes_);
    }

    virtual void TearDown() {
      free(mix_buffer_);
      free(compare_buffer_);
      free(src_buffer_);
    }

  int32_t *mix_buffer_;
  int32_t *src_buffer_;
  int32_t *compare_buffer_;
  snd_pcm_format_t fmt_;
  unsigned int fr_bytes_;
};

TEST_F(MixTestSuiteS24_LE, MixFirst) {
  cras_mix_add(fmt_, (uint8_t *)mix_buffer_, (uint8_t *)src_buffer_,
                      kNumSamples, 0, 0, 1.0);
  EXPECT_EQ(0, memcmp(mix_buffer_, src_buffer_, kBufferFrames * fr_bytes_));
}

TEST_F(MixTestSuiteS24_LE, MixTwo) {
  cras_mix_add(fmt_, (uint8_t *)mix_buffer_, (uint8_t *)src_buffer_,
               kNumSamples, 0, 0, 1.0);
  cras_mix_add(fmt_, (uint8_t *)mix_buffer_, (uint8_t *)src_buffer_,
               kNumSamples, 1, 0, 1.0);

  for (size_t i = 0; i < kBufferFrames * 2; i++)
    compare_buffer_[i] = src_buffer_[i] * 2;
  EXPECT_EQ(0, memcmp(mix_buffer_, compare_buffer_, kBufferFrames * fr_bytes_));
}

TEST_F(MixTestSuiteS24_LE, MixTwoClip) {
  cras_mix_add(fmt_, (uint8_t *)mix_buffer_, (uint8_t *)src_buffer_,
               kNumSamples, 0, 0, 1.0);
  for (size_t i = 0; i < kBufferFrames * 2; i++)
    src_buffer_[i] = 0x007fffff;
  cras_mix_add(fmt_, (uint8_t *)mix_buffer_, (uint8_t *)src_buffer_,
               kNumSamples, 1, 0, 1.0);

  for (size_t i = 0; i < kBufferFrames * 2; i++)
    compare_buffer_[i] = 0x007fffff;
  EXPECT_EQ(0, memcmp(mix_buffer_, compare_buffer_, kBufferFrames * fr_bytes_));
}

TEST_F(MixTestSuiteS24_LE, MixFirstMuted) {
  cras_mix_add(fmt_, (uint8_t *)mix_buffer_, (uint8_t *)src_buffer_,
               kNumSamples, 0, 1, 1.0);

  for (size_t i = 0; i < kBufferFrames * 2; i++)
    compare_buffer_[i] = 0;
  EXPECT_EQ(0, memcmp(mix_buffer_, compare_buffer_, kBufferFrames * fr_bytes_));
}

TEST_F(MixTestSuiteS24_LE, MixFirstZeroVolume) {
  cras_mix_add(fmt_, (uint8_t *)mix_buffer_, (uint8_t *)src_buffer_,
                      kNumSamples, 0, 0, 0.0);

  for (size_t i = 0; i < kBufferFrames * 2; i++)
    compare_buffer_[i] = 0;
  EXPECT_EQ(0, memcmp(mix_buffer_, compare_buffer_, kBufferFrames * fr_bytes_));
}

TEST_F(MixTestSuiteS24_LE, MixFirstHalfVolume) {
  cras_mix_add(fmt_, (uint8_t *)mix_buffer_, (uint8_t *)src_buffer_,
                      kNumSamples, 0, 0, 0.5);

  for (size_t i = 0; i < kBufferFrames * 2; i++)
    compare_buffer_[i] = src_buffer_[i] * 0.5;
  EXPECT_EQ(0, memcmp(mix_buffer_, compare_buffer_, kBufferFrames * fr_bytes_));
}

TEST_F(MixTestSuiteS24_LE, MixTwoSecondHalfVolume) {
  cras_mix_add(fmt_, (uint8_t *)mix_buffer_, (uint8_t *)src_buffer_,
               kNumSamples, 0, 0, 1.0);
  cras_mix_add(fmt_, (uint8_t *)mix_buffer_, (uint8_t *)src_buffer_,
               kNumSamples, 1, 0, 0.5);

  for (size_t i = 0; i < kBufferFrames * 2; i++)
    compare_buffer_[i] = src_buffer_[i] + (int32_t)(src_buffer_[i] * 0.5);
  EXPECT_EQ(0, memcmp(mix_buffer_, compare_buffer_, kBufferFrames * fr_bytes_));
}

TEST_F(MixTestSuiteS24_LE, ScaleFullVolume) {
  memcpy(compare_buffer_, src_buffer_, kBufferFrames  * fr_bytes_);
  cras_scale_buffer(fmt_, (uint8_t *)mix_buffer_, kNumSamples, 0.999999999);

  EXPECT_EQ(0, memcmp(compare_buffer_, src_buffer_, kBufferFrames * fr_bytes_));
}

TEST_F(MixTestSuiteS24_LE, ScaleMinVolume) {
  memset(compare_buffer_, 0, kBufferFrames * fr_bytes_);
  cras_scale_buffer(fmt_, (uint8_t *)src_buffer_, kNumSamples, 0.0000000001);

  EXPECT_EQ(0, memcmp(compare_buffer_, src_buffer_, kBufferFrames * fr_bytes_));
}

TEST_F(MixTestSuiteS24_LE, ScaleHalfVolume) {
  for (size_t i = 0; i < kBufferFrames * 2; i++)
    compare_buffer_[i] = src_buffer_[i] * 0.5;
  cras_scale_buffer(fmt_, (uint8_t *)src_buffer_, kNumSamples, 0.5);

  EXPECT_EQ(0, memcmp(compare_buffer_, src_buffer_, kBufferFrames * fr_bytes_));
}

class MixTestSuiteS32_LE : public testing::Test{
  protected:
    virtual void SetUp() {
      fmt_ = SND_PCM_FORMAT_S32_LE;
      fr_bytes_ = 4 * kNumChannels;
      mix_buffer_ = (int32_t *)malloc(kBufferFrames * fr_bytes_);
      src_buffer_ = static_cast<int32_t *>(
          calloc(1, kBufferFrames * fr_bytes_ + sizeof(cras_audio_shm_area)));

      for (size_t i = 0; i < kBufferFrames * 2; i++) {
        src_buffer_[i] = i;
        mix_buffer_[i] = -i;
      }

      compare_buffer_ = (int32_t *)malloc(kBufferFrames * fr_bytes_);
    }

    virtual void TearDown() {
      free(mix_buffer_);
      free(compare_buffer_);
      free(src_buffer_);
    }

  int32_t *mix_buffer_;
  int32_t *src_buffer_;
  int32_t *compare_buffer_;
  snd_pcm_format_t fmt_;
  unsigned int fr_bytes_;
};

TEST_F(MixTestSuiteS32_LE, MixFirst) {
  cras_mix_add(fmt_, (uint8_t *)mix_buffer_, (uint8_t *)src_buffer_,
                      kNumSamples, 0, 0, 1.0);
  EXPECT_EQ(0, memcmp(mix_buffer_, src_buffer_, kBufferFrames * fr_bytes_));
}

TEST_F(MixTestSuiteS32_LE, MixTwo) {
  cras_mix_add(fmt_, (uint8_t *)mix_buffer_, (uint8_t *)src_buffer_,
               kNumSamples, 0, 0, 1.0);
  cras_mix_add(fmt_, (uint8_t *)mix_buffer_, (uint8_t *)src_buffer_,
               kNumSamples, 1, 0, 1.0);

  for (size_t i = 0; i < kBufferFrames * 2; i++)
    compare_buffer_[i] = src_buffer_[i] * 2;
  EXPECT_EQ(0, memcmp(mix_buffer_, compare_buffer_, kBufferFrames * fr_bytes_));
}

TEST_F(MixTestSuiteS32_LE, MixTwoClip) {
  cras_mix_add(fmt_, (uint8_t *)mix_buffer_, (uint8_t *)src_buffer_,
               kNumSamples, 0, 0, 1.0);
  for (size_t i = 0; i < kBufferFrames * 2; i++)
    src_buffer_[i] = INT32_MAX;
  cras_mix_add(fmt_, (uint8_t *)mix_buffer_, (uint8_t *)src_buffer_,
               kNumSamples, 1, 0, 1.0);

  for (size_t i = 0; i < kBufferFrames * 2; i++)
    compare_buffer_[i] = INT32_MAX;
  EXPECT_EQ(0, memcmp(mix_buffer_, compare_buffer_, kBufferFrames * fr_bytes_));
}

TEST_F(MixTestSuiteS32_LE, MixFirstMuted) {
  cras_mix_add(fmt_, (uint8_t *)mix_buffer_, (uint8_t *)src_buffer_,
               kNumSamples, 0, 1, 1.0);

  for (size_t i = 0; i < kBufferFrames * 2; i++)
    compare_buffer_[i] = 0;
  EXPECT_EQ(0, memcmp(mix_buffer_, compare_buffer_, kBufferFrames * fr_bytes_));
}

TEST_F(MixTestSuiteS32_LE, MixFirstZeroVolume) {
  cras_mix_add(fmt_, (uint8_t *)mix_buffer_, (uint8_t *)src_buffer_,
                      kNumSamples, 0, 0, 0.0);

  for (size_t i = 0; i < kBufferFrames * 2; i++)
    compare_buffer_[i] = 0;
  EXPECT_EQ(0, memcmp(mix_buffer_, compare_buffer_, kBufferFrames * fr_bytes_));
}

TEST_F(MixTestSuiteS32_LE, MixFirstHalfVolume) {
  cras_mix_add(fmt_, (uint8_t *)mix_buffer_, (uint8_t *)src_buffer_,
                      kNumSamples, 0, 0, 0.5);

  for (size_t i = 0; i < kBufferFrames * 2; i++)
    compare_buffer_[i] = src_buffer_[i] * 0.5;
  EXPECT_EQ(0, memcmp(mix_buffer_, compare_buffer_, kBufferFrames * fr_bytes_));
}

TEST_F(MixTestSuiteS32_LE, MixTwoSecondHalfVolume) {
  cras_mix_add(fmt_, (uint8_t *)mix_buffer_, (uint8_t *)src_buffer_,
               kNumSamples, 0, 0, 1.0);
  cras_mix_add(fmt_, (uint8_t *)mix_buffer_, (uint8_t *)src_buffer_,
               kNumSamples, 1, 0, 0.5);

  for (size_t i = 0; i < kBufferFrames * 2; i++)
    compare_buffer_[i] = src_buffer_[i] + (int32_t)(src_buffer_[i] * 0.5);
  EXPECT_EQ(0, memcmp(mix_buffer_, compare_buffer_, kBufferFrames * fr_bytes_));
}

TEST_F(MixTestSuiteS32_LE, ScaleFullVolume) {
  memcpy(compare_buffer_, src_buffer_, kBufferFrames  * fr_bytes_);
  cras_scale_buffer(fmt_, (uint8_t *)mix_buffer_, kNumSamples, 0.999999999);

  EXPECT_EQ(0, memcmp(compare_buffer_, src_buffer_, kBufferFrames * fr_bytes_));
}

TEST_F(MixTestSuiteS32_LE, ScaleMinVolume) {
  memset(compare_buffer_, 0, kBufferFrames * fr_bytes_);
  cras_scale_buffer(fmt_, (uint8_t *)src_buffer_, kNumSamples, 0.0000000001);

  EXPECT_EQ(0, memcmp(compare_buffer_, src_buffer_, kBufferFrames * fr_bytes_));
}

TEST_F(MixTestSuiteS32_LE, ScaleHalfVolume) {
  for (size_t i = 0; i < kBufferFrames * 2; i++)
    compare_buffer_[i] = src_buffer_[i] * 0.5;
  cras_scale_buffer(fmt_, (uint8_t *)src_buffer_, kNumSamples, 0.5);

  EXPECT_EQ(0, memcmp(compare_buffer_, src_buffer_, kBufferFrames * fr_bytes_));
}

/* Stubs */
extern "C" {

}  // extern "C"

}  //  namespace

int main(int argc, char **argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
