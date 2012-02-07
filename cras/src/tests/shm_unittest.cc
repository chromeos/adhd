// Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stdio.h>
#include <gtest/gtest.h>

extern "C" {
#include "cras_shm.h"
#include "cras_types.h"
}

namespace {

class ShmTestSuite : public testing::Test{
  protected:
    virtual void SetUp() {
      memset(&shm_, 0, sizeof(shm_));
      shm_.frame_bytes = 4;
      frames_ = 0;
    }

    struct cras_audio_shm_area shm_;
    int16_t *buf_;
    size_t frames_;
};

// Test that and empty buffer returns 0 readable bytes.
TEST_F(ShmTestSuite, NoneReadableWhenEmpty) {
  buf_ = cras_shm_get_readable_frames(&shm_, 0, &frames_);
  EXPECT_EQ(0, frames_);
  cras_shm_buffer_read(&shm_, frames_);
  EXPECT_EQ(0, shm_.read_offset[0]);
}

// Buffer with 100 frames filled.
TEST_F(ShmTestSuite, OneHundredFilled) {
  shm_.write_offset[0] = 100 * shm_.frame_bytes;
  buf_ = cras_shm_get_readable_frames(&shm_, 0, &frames_);
  EXPECT_EQ(100, frames_);
  EXPECT_EQ((int16_t *)shm_.samples, buf_);
  cras_shm_buffer_read(&shm_, frames_ - 10);
  EXPECT_EQ((frames_ - 10) * shm_.frame_bytes, shm_.read_offset[0]);
  cras_shm_buffer_read(&shm_, 10);
  EXPECT_EQ(0, shm_.read_offset[0]);
  EXPECT_EQ(1, shm_.read_buf_idx);
}

// Buffer with 100 frames filled, 50 read.
TEST_F(ShmTestSuite, OneHundredFilled50Read) {
  shm_.write_offset[0] = 100 * shm_.frame_bytes;
  shm_.read_offset[0] = 50 * shm_.frame_bytes;
  buf_ = cras_shm_get_readable_frames(&shm_, 0, &frames_);
  EXPECT_EQ(50, frames_);
  EXPECT_EQ((int16_t *)(shm_.samples + shm_.read_offset[0]), buf_);
  cras_shm_buffer_read(&shm_, frames_ - 10);
  EXPECT_EQ(shm_.write_offset[0] - 10 * shm_.frame_bytes, shm_.read_offset[0]);
  cras_shm_buffer_read(&shm_, 10);
  EXPECT_EQ(0, shm_.read_offset[0]);
}

// Buffer with 100 frames filled, 50 read, offset by 25.
TEST_F(ShmTestSuite, OneHundredFilled50Read25offset) {
  shm_.write_offset[0] = 100 * shm_.frame_bytes;
  shm_.read_offset[0] = 50 * shm_.frame_bytes;
  buf_ = cras_shm_get_readable_frames(&shm_, 25, &frames_);
  EXPECT_EQ(25, frames_);
  EXPECT_EQ(shm_.samples + shm_.read_offset[0] + 25 * shm_.frame_bytes,
      (uint8_t *)buf_);
}

// Test wrapping across buffers.
TEST_F(ShmTestSuite, WrapToNextBuffer) {
  shm_.used_size = 480 * shm_.frame_bytes;
  shm_.write_offset[0] = 240 * shm_.frame_bytes;
  shm_.read_offset[0] = 120 * shm_.frame_bytes;
  shm_.write_offset[1] = 240 * shm_.frame_bytes;
  buf_ = cras_shm_get_readable_frames(&shm_, 0, &frames_);
  EXPECT_EQ(120, frames_);
  EXPECT_EQ(shm_.samples + shm_.read_offset[0], (uint8_t *)buf_);
  buf_ = cras_shm_get_readable_frames(&shm_, frames_, &frames_);
  EXPECT_EQ(240, frames_);
  EXPECT_EQ(shm_.samples + shm_.used_size, (uint8_t *)buf_);
  cras_shm_buffer_read(&shm_, 350); /* Mark all-10 as read */
  EXPECT_EQ(0, shm_.read_offset[0]);
  EXPECT_EQ(230 * shm_.frame_bytes, shm_.read_offset[1]);
}

// Test wrapping last buffer.
TEST_F(ShmTestSuite, WrapFromFinalBuffer) {
  shm_.read_buf_idx = CRAS_NUM_SHM_BUFFERS - 1;
  shm_.used_size = 480 * shm_.frame_bytes;
  shm_.write_offset[shm_.read_buf_idx] = 240 * shm_.frame_bytes;
  shm_.read_offset[shm_.read_buf_idx] = 120 * shm_.frame_bytes;
  shm_.write_offset[0] = 240 * shm_.frame_bytes;
  buf_ = cras_shm_get_readable_frames(&shm_, 0, &frames_);
  EXPECT_EQ(120, frames_);
  EXPECT_EQ((uint8_t *)buf_, shm_.samples +
      shm_.used_size * shm_.read_buf_idx +
      shm_.read_offset[shm_.read_buf_idx]);
  buf_ = cras_shm_get_readable_frames(&shm_, frames_, &frames_);
  EXPECT_EQ(240, frames_);
  EXPECT_EQ(shm_.samples, (uint8_t *)buf_);
  cras_shm_buffer_read(&shm_, 350); /* Mark all-10 as read */
  EXPECT_EQ(0, shm_.read_offset[1]);
  EXPECT_EQ(230 * shm_.frame_bytes, shm_.read_offset[0]);
}

TEST_F(ShmTestSuite, SetVolume) {
  cras_shm_set_volume(&shm_, 1.0);
  EXPECT_EQ(shm_.volume, 1.0);
  cras_shm_set_volume(&shm_, 1.4);
  EXPECT_EQ(shm_.volume, 1.0);
  cras_shm_set_volume(&shm_, -0.5);
  EXPECT_EQ(shm_.volume, 0.0);
  cras_shm_set_volume(&shm_, 0.5);
  EXPECT_EQ(shm_.volume, 0.5);
}

}  //  namespace

int main(int argc, char **argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
