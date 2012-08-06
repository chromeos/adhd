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

static int cras_system_get_mute_return;

class MixTestSuite : public testing::Test{
  protected:
    virtual void SetUp() {
      int16_t *buf;

      mix_buffer_ = (int16_t *)malloc(kBufferFrames * 4);
      mix_index_ = 0;

      shm_ = (struct cras_audio_shm_area *)calloc(1,
          sizeof(struct cras_audio_shm_area) + kBufferFrames * 4);
      cras_shm_set_frame_bytes(shm_, 4);

      buf = (int16_t *)shm_->samples;
      for (size_t i = 0; i < kBufferFrames * 2; i++) {
        buf[i] = i;
        mix_buffer_[i] = -i;
      }
      shm_->write_offset[0] = kBufferFrames * 4;
      shm_->mute = 0;
      shm_->volume_scaler = 1.0;

      compare_buffer_ = (int16_t *)malloc(kBufferFrames * 4);
    }

    virtual void TearDown() {
      free(mix_buffer_);
      free(compare_buffer_);
      free(shm_);
    }

  int16_t *mix_buffer_;
  size_t mix_index_;
  int16_t *compare_buffer_;
  struct cras_audio_shm_area *shm_;
};

TEST_F(MixTestSuite, MixFirst) {
  size_t count = kBufferFrames;
  cras_system_get_mute_return = 0;
  cras_mix_add_stream(
      shm_, kNumChannels, (uint8_t *)mix_buffer_, &count, &mix_index_);
  EXPECT_EQ(kBufferFrames, count);
  EXPECT_EQ(1, mix_index_);
  EXPECT_EQ(0, memcmp(mix_buffer_, shm_->samples, kBufferFrames*4));
}

TEST_F(MixTestSuite, MixFirstSystemMuted) {
  size_t count = kBufferFrames;
  cras_system_get_mute_return = 1;
  cras_mix_add_stream(
      shm_, kNumChannels, (uint8_t *)mix_buffer_, &count, &mix_index_);
  EXPECT_EQ(kBufferFrames, count);
  EXPECT_EQ(1, mix_index_);
  memset(compare_buffer_, 0, kBufferFrames * 4);
  EXPECT_EQ(0, memcmp(mix_buffer_, compare_buffer_, kBufferFrames*4));
}

TEST_F(MixTestSuite, MixTwo) {
  size_t count = kBufferFrames;
  int16_t *buf;

  cras_system_get_mute_return = 0;
  cras_mix_add_stream(
      shm_, kNumChannels, (uint8_t *)mix_buffer_, &count, &mix_index_);
  cras_mix_add_stream(
      shm_, kNumChannels, (uint8_t *)mix_buffer_, &count, &mix_index_);
  EXPECT_EQ(kBufferFrames, count);
  EXPECT_EQ(2, mix_index_);

  buf = (int16_t *)shm_->samples;
  for (size_t i = 0; i < kBufferFrames * 2; i++)
    compare_buffer_[i] = buf[i] * 2;
  EXPECT_EQ(0, memcmp(mix_buffer_, compare_buffer_, kBufferFrames*4));
}

TEST_F(MixTestSuite, MixTwoSystemMuted) {
  size_t count = kBufferFrames;

  cras_system_get_mute_return = 1;
  cras_mix_add_stream(
      shm_, kNumChannels, (uint8_t *)mix_buffer_, &count, &mix_index_);
  cras_mix_add_stream(
      shm_, kNumChannels, (uint8_t *)mix_buffer_, &count, &mix_index_);
  EXPECT_EQ(kBufferFrames, count);
  EXPECT_EQ(2, mix_index_);

  memset(compare_buffer_, 0, kBufferFrames * 4);
  EXPECT_EQ(0, memcmp(mix_buffer_, compare_buffer_, kBufferFrames*4));
}

TEST_F(MixTestSuite, MixFirstMuted) {
  size_t count = kBufferFrames;

  shm_->mute = 1;
  cras_system_get_mute_return = 0;
  cras_mix_add_stream(
      shm_, kNumChannels, (uint8_t *)mix_buffer_, &count, &mix_index_);
  EXPECT_EQ(kBufferFrames, count);
  EXPECT_EQ(1, mix_index_);

  for (size_t i = 0; i < kBufferFrames * 2; i++)
    compare_buffer_[i] = 0;
  EXPECT_EQ(0, memcmp(mix_buffer_, compare_buffer_, kBufferFrames*4));
}

TEST_F(MixTestSuite, MixFirstMutedSystemMuted) {
  size_t count = kBufferFrames;

  shm_->mute = 1;
  cras_system_get_mute_return = 1;
  cras_mix_add_stream(
      shm_, kNumChannels, (uint8_t *)mix_buffer_, &count, &mix_index_);
  EXPECT_EQ(kBufferFrames, count);
  EXPECT_EQ(1, mix_index_);

  memset(compare_buffer_, 0, kBufferFrames * 4);
  EXPECT_EQ(0, memcmp(mix_buffer_, compare_buffer_, kBufferFrames*4));
}

TEST_F(MixTestSuite, MixFirstZeroVolume) {
  size_t count = kBufferFrames;

  shm_->volume_scaler = 0.0;
  cras_system_get_mute_return = 0;
  cras_mix_add_stream(
      shm_, kNumChannels, (uint8_t *)mix_buffer_, &count, &mix_index_);
  EXPECT_EQ(kBufferFrames, count);
  EXPECT_EQ(1, mix_index_);

  for (size_t i = 0; i < kBufferFrames * 2; i++)
    compare_buffer_[i] = 0;
  EXPECT_EQ(0, memcmp(mix_buffer_, compare_buffer_, kBufferFrames*4));
}

TEST_F(MixTestSuite, MixFirstHalfVolume) {
  size_t count = kBufferFrames;
  int16_t *buf;

  shm_->volume_scaler = 0.5;
  cras_system_get_mute_return = 0;
  cras_mix_add_stream(
      shm_, kNumChannels, (uint8_t *)mix_buffer_, &count, &mix_index_);
  EXPECT_EQ(kBufferFrames, count);
  EXPECT_EQ(1, mix_index_);

  buf = (int16_t *)shm_->samples;
  for (size_t i = 0; i < kBufferFrames * 2; i++)
    compare_buffer_[i] = buf[i] * 0.5;
  EXPECT_EQ(0, memcmp(mix_buffer_, compare_buffer_, kBufferFrames*4));
}

TEST_F(MixTestSuite, MixTwoSecondHalfVolume) {
  size_t count = kBufferFrames;
  int16_t *buf;

  shm_->volume_scaler = 1.0;
  cras_system_get_mute_return = 0;
  cras_mix_add_stream(
      shm_, kNumChannels, (uint8_t *)mix_buffer_, &count, &mix_index_);
  EXPECT_EQ(kBufferFrames, count);
  EXPECT_EQ(1, mix_index_);
  shm_->volume_scaler = 0.5;
  cras_mix_add_stream(
      shm_, kNumChannels, (uint8_t *)mix_buffer_, &count, &mix_index_);
  EXPECT_EQ(kBufferFrames, count);
  EXPECT_EQ(2, mix_index_);

  buf = (int16_t *)shm_->samples;
  for (size_t i = 0; i < kBufferFrames * 2; i++)
    compare_buffer_[i] = buf[i] + (int16_t)(buf[i] * 0.5);
  EXPECT_EQ(0, memcmp(mix_buffer_, compare_buffer_, kBufferFrames*4));
}

TEST_F(MixTestSuite, MixTwoSecondHalfVolumeSystemMuted) {
  size_t count = kBufferFrames;

  shm_->volume_scaler = 1.0;
  cras_system_get_mute_return = 1;
  cras_mix_add_stream(
      shm_, kNumChannels, (uint8_t *)mix_buffer_, &count, &mix_index_);
  EXPECT_EQ(kBufferFrames, count);
  EXPECT_EQ(1, mix_index_);
  shm_->volume_scaler = 0.5;
  cras_mix_add_stream(
      shm_, kNumChannels, (uint8_t *)mix_buffer_, &count, &mix_index_);
  EXPECT_EQ(kBufferFrames, count);
  EXPECT_EQ(2, mix_index_);

  memset(compare_buffer_, 0, kBufferFrames * 4);
  EXPECT_EQ(0, memcmp(mix_buffer_, compare_buffer_, kBufferFrames*4));
}

/* Stubs */
extern "C" {

int cras_system_get_mute() {
  return cras_system_get_mute_return;
}

}  // extern "C"

}  //  namespace

int main(int argc, char **argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
