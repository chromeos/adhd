// Copyright (c) 2013 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stdio.h>
#include <stdlib.h>
#include <gtest/gtest.h>

extern "C" {
#include "cras_audio_area.h"
#include "cras_iodev.h"
#include "cras_loopback_iodev.h"
#include "cras_shm.h"
#include "cras_types.h"
}

namespace {

static const unsigned int kBufferFrames = 16384;
static const unsigned int kFrameBytes = 4;
static const unsigned int kBufferSize = kBufferFrames * kFrameBytes;
static cras_audio_area *dummy_audio_area;

class LoopBackTestSuite : public testing::Test{
  protected:
    virtual void SetUp() {
      for (unsigned int i = 0; i < kBufferSize; i++) {
        buf_[i] = rand();
      }

      dev_ = loopback_iodev_create(CRAS_STREAM_POST_MIX_PRE_DSP);
      fmt_.frame_rate = 44100;
      fmt_.num_channels = 2;
      fmt_.format = SND_PCM_FORMAT_S16_LE;
      dev_->format = &fmt_;

      dummy_audio_area = (cras_audio_area*)calloc(1,
          sizeof(*dummy_audio_area) + sizeof(cras_channel_area) * 2);
    }

    virtual void TearDown() {
      loopback_iodev_destroy(dev_);
      free(dummy_audio_area);
    }

  uint8_t buf_[kBufferSize];
  unsigned int frame_bytes;
  struct cras_iodev *dev_;
  struct cras_audio_format fmt_;
};

TEST_F(LoopBackTestSuite, CopyReturnZero) {
  // Test that no frames are returned until the buffer level is met.
  unsigned int frames = 200;
  int rc = dev_->open_dev(dev_);
  EXPECT_EQ(0, rc);
  rc = loopback_iodev_add_audio(dev_, buf_, frames);
  EXPECT_EQ(0, rc);

  cras_audio_area *dev_area;
  rc = dev_->get_buffer(dev_, &dev_area, &frames);
  EXPECT_EQ(0, rc);
  EXPECT_EQ(0, frames);
  EXPECT_EQ(0, dev_area->frames);
  rc = dev_->close_dev(dev_);
  EXPECT_EQ(0, rc);
}

TEST_F(LoopBackTestSuite, SimpleCopy) {
  // Fill with the minimum pore-buffer
  unsigned int frames = 200;
  int rc = dev_->open_dev(dev_);
  EXPECT_EQ(0, rc);
  rc = loopback_iodev_add_audio(dev_, buf_, 4096);
  EXPECT_EQ(0, rc);

  cras_audio_area *dev_area;
  rc = dev_->get_buffer(dev_, &dev_area, &frames);
  EXPECT_EQ(0, rc);
  EXPECT_EQ(200, frames);
  EXPECT_EQ(0, memcmp(buf_, dev_area->channels[0].buf, 200 * kFrameBytes));
  dev_->put_buffer(dev_, frames);

  frames = 4000;
  rc = dev_->get_buffer(dev_, &dev_area, &frames);
  EXPECT_EQ(0, rc);
  EXPECT_EQ(4096 - 200, frames);
  EXPECT_EQ(0, memcmp(buf_ + 200 * kFrameBytes,
            dev_area->channels[0].buf, (4096 - 200) * kFrameBytes));
  dev_->put_buffer(dev_, frames);
  rc = dev_->close_dev(dev_);
  EXPECT_EQ(0, rc);
}

TEST_F(LoopBackTestSuite, WrapCopy) {
  // Fill with the minimum pore-buffer
  unsigned int frames = 8000;
  int rc = dev_->open_dev(dev_);
  EXPECT_EQ(0, rc);
  rc = loopback_iodev_add_audio(dev_, buf_, 8000);
  EXPECT_EQ(0, rc);

  cras_audio_area *dev_area;
  rc = dev_->get_buffer(dev_, &dev_area, &frames);
  EXPECT_EQ(0, rc);
  EXPECT_EQ(8000, frames);
  EXPECT_EQ(0, memcmp(buf_, dev_area->channels[0].buf, 200 * kFrameBytes));
  dev_->put_buffer(dev_, frames);

  rc = loopback_iodev_add_audio(dev_, buf_, 8000);
  EXPECT_EQ(0, rc);

  frames = 8000;
  rc = dev_->get_buffer(dev_, &dev_area, &frames);
  EXPECT_EQ(0, rc);
  EXPECT_EQ(192, frames);
  EXPECT_EQ(0, memcmp(buf_, dev_area->channels[0].buf, 192 * kFrameBytes));
  dev_->put_buffer(dev_, frames);
  EXPECT_EQ(0, rc);

  frames = 8000;
  rc = dev_->get_buffer(dev_, &dev_area, &frames);
  EXPECT_EQ(0, rc);
  EXPECT_EQ(8000 - 192, frames);
  EXPECT_EQ(0, memcmp(buf_ + 192 * kFrameBytes,
            dev_area->channels[0].buf, (8000 - 192) * kFrameBytes));
  dev_->put_buffer(dev_, frames);
  rc = dev_->close_dev(dev_);
  EXPECT_EQ(0, rc);
}

/* Stubs */
extern "C" {

int cras_iodev_list_add_input(struct cras_iodev *input) {
  return 0;
}

int cras_iodev_list_rm_input(struct cras_iodev *dev) {
  return 0;
}

void cras_iodev_free_format(struct cras_iodev *iodev)
{
}

void cras_iodev_init_audio_area(struct cras_iodev *iodev,
                                int num_channels) {
  iodev->area = dummy_audio_area;
}

void cras_iodev_free_audio_area(struct cras_iodev *iodev) {
}

void cras_audio_area_config_buf_pointers(struct cras_audio_area *area,
					 const struct cras_audio_format *fmt,
					 uint8_t *base_buffer)
{
  dummy_audio_area->channels[0].buf = base_buffer;
}
}  // extern "C"

}  //  namespace

int main(int argc, char **argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
