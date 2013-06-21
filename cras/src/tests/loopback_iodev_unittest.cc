// Copyright (c) 2013 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stdio.h>
#include <stdlib.h>
#include <gtest/gtest.h>

extern "C" {
#include "cras_loopback_iodev.h"
#include "cras_rstream.h"
#include "cras_shm.h"
#include "cras_types.h"
}

namespace {

static const unsigned int kBufferFrames = 4096;
static const unsigned int kFrameBytes = 4;
static const unsigned int kBufferSize = kBufferFrames * kFrameBytes;
static unsigned int cras_rstream_audio_ready_called;
static unsigned int cras_rstream_audio_ready_return;

class LoopBackTestSuite : public testing::Test{
  protected:
    virtual void SetUp() {
      shm_ = &rstream_.input_shm;
      memset(shm_, 0, sizeof(*shm_));

      shm_->area = static_cast<struct cras_audio_shm_area *>(
          calloc(1, kBufferSize * 2 + sizeof(*shm_->area)));
      memset(shm_->area->samples, 0x55, kBufferSize);
      cras_shm_set_frame_bytes(shm_, kFrameBytes);
      cras_shm_set_used_size(shm_, kBufferSize);
      cras_shm_set_volume_scaler(shm_, 1.0);

      rstream_.direction = CRAS_STREAM_POST_MIX_PRE_DSP;
      rstream_.cb_threshold = 200;

      cras_rstream_audio_ready_called = 0;

      for (unsigned int i = 0; i < kBufferSize; i++) {
        buf_[i] = rand();
      }
    }

    virtual void TearDown() {
      free(shm_->area);
    }

  struct cras_rstream rstream_;
  struct cras_audio_shm *shm_;
  uint8_t buf_[kBufferSize];
  unsigned int frame_bytes;
};

TEST_F(LoopBackTestSuite, FailNotLoopback) {
      rstream_.direction = CRAS_STREAM_INPUT;
      EXPECT_EQ(-EINVAL,
                loopback_iodev_add_audio(NULL, buf_, 200, &rstream_));
}

TEST_F(LoopBackTestSuite, SimpleCopy) {
  loopback_iodev_add_audio(NULL, buf_, 200, &rstream_);
  EXPECT_EQ(1, cras_rstream_audio_ready_called);
  EXPECT_EQ(0, memcmp(buf_, shm_->area->samples, 200 * kFrameBytes));
}

TEST_F(LoopBackTestSuite, CopyLessThanThreshold) {
  // 150 frames, all to buf 0.
  loopback_iodev_add_audio(NULL, buf_, 150, &rstream_);
  EXPECT_EQ(0, cras_rstream_audio_ready_called);
  EXPECT_EQ(0, memcmp(buf_, shm_->area->samples, 150 * kFrameBytes));

  // 100 frames, 50 to buf 0, 50 to buf 1.
  loopback_iodev_add_audio(NULL, &buf_[150 * kFrameBytes], 100, &rstream_);
  EXPECT_EQ(1, cras_rstream_audio_ready_called);
  EXPECT_EQ(0, memcmp(buf_, shm_->area->samples, 200 * kFrameBytes));
  // Overflow should be coppied to the other buffer.
  EXPECT_EQ(0, memcmp(&buf_[200 * kFrameBytes],
                      shm_->area->samples + shm_->area->config.used_size,
                      50 * kFrameBytes));
}

TEST_F(LoopBackTestSuite, CopyMoreThanThreshold) {
  // 250 frames, 200 to buf 0, 50 to buf 1.
  loopback_iodev_add_audio(NULL, buf_, 250, &rstream_);
  EXPECT_EQ(1, cras_rstream_audio_ready_called);
  EXPECT_EQ(0, memcmp(buf_, shm_->area->samples, 200 * kFrameBytes));
  // Overflow should be coppied to the other buffer.
  EXPECT_EQ(0, memcmp(&buf_[200 * kFrameBytes],
                      shm_->area->samples + shm_->area->config.used_size,
                      50 * kFrameBytes));

  // 200 frames, 150 to buf 1, 50 to buf 0.
  loopback_iodev_add_audio(NULL, &buf_[250 * kFrameBytes], 200, &rstream_);
  EXPECT_EQ(2, cras_rstream_audio_ready_called);
  EXPECT_EQ(0, memcmp(&buf_[200 * kFrameBytes],
                      shm_->area->samples + shm_->area->config.used_size,
                      200 * kFrameBytes));
  // Overflow should be coppied to the other buffer.
  EXPECT_EQ(0, memcmp(&buf_[400 * kFrameBytes],
                      shm_->area->samples,
                      50 * kFrameBytes));
}

/* Stubs */
extern "C" {

int cras_rstream_audio_ready(const struct cras_rstream *stream, size_t count) {
  cras_rstream_audio_ready_called++;
  return cras_rstream_audio_ready_return;;
}

int cras_iodev_list_add_input(struct cras_iodev *input) {
  return 0;
}

int cras_iodev_list_rm_input(struct cras_iodev *dev) {
  return 0;
}

}  // extern "C"

}  //  namespace

int main(int argc, char **argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
