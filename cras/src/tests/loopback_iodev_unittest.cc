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
      dummy_audio_area = (cras_audio_area*)calloc(
          1, sizeof(*dummy_audio_area) + sizeof(cras_channel_area) * 2);
      for (unsigned int i = 0; i < kBufferSize; i++) {
        buf_[i] = rand();
      }
      fmt_.frame_rate = 44100;
      fmt_.num_channels = 2;
      fmt_.format = SND_PCM_FORMAT_S16_LE;

      loopback_iodev_create(&loop_in_, &loop_out_);
      loop_in_->format = &fmt_;
      loop_out_->format = &fmt_;
    }

    virtual void TearDown() {
      loopback_iodev_destroy(loop_in_, loop_out_);
      free(dummy_audio_area);
    }

    uint8_t buf_[kBufferSize];
    struct cras_audio_format fmt_;
    struct cras_iodev *loop_in_, *loop_out_;
};

TEST_F(LoopBackTestSuite, OpenAndCloseDevice) {
  int rc;

  // Open loopback devices.
  rc = loop_out_->open_dev(loop_out_);
  EXPECT_EQ(rc, 0);
  rc = loop_in_->open_dev(loop_in_);
  EXPECT_EQ(rc, 0);

  // Check device open status.
  rc = loop_out_->is_open(loop_out_);
  EXPECT_EQ(rc, 1);
  rc = loop_in_->is_open(loop_in_);
  EXPECT_EQ(rc, 1);

  // Check zero frames queued.
  rc = loop_out_->frames_queued(loop_out_);
  EXPECT_EQ(rc, 0);
  rc = loop_in_->frames_queued(loop_in_);
  EXPECT_EQ(rc, 0);

  // Close loopback devices.
  rc = loop_in_->close_dev(loop_in_);
  EXPECT_EQ(rc, 0);
  rc = loop_out_->close_dev(loop_out_);
  EXPECT_EQ(rc, 0);

  // Check device open status.
  rc = loop_out_->is_open(loop_out_);
  EXPECT_EQ(rc, 0);
  rc = loop_in_->is_open(loop_in_);
  EXPECT_EQ(rc, 0);
}

TEST_F(LoopBackTestSuite, SimpleLoopback) {
  static cras_audio_area *area;
  unsigned int nread = 1024;
  int rc;

  loop_out_->open_dev(loop_out_);
  loop_in_->open_dev(loop_in_);

  // Copy frames to loopback playback.
  loop_out_->get_buffer(loop_out_, &area, &nread);
  EXPECT_EQ(nread, 1024);
  memcpy(area->channels[0].buf, buf_, nread);
  loop_out_->put_buffer(loop_out_, nread);

  // Check frames queued.
  rc = loop_out_->frames_queued(loop_out_);
  EXPECT_EQ(rc, 1024);

  // Verify frames from loopback record.
  loop_in_->get_buffer(loop_in_, &area, &nread);
  EXPECT_EQ(nread, 1024);
  rc = memcmp(area->channels[0].buf, buf_, nread);
  EXPECT_EQ(rc, 0);
  loop_in_->put_buffer(loop_in_, nread);

  // Check zero frames queued.
  rc = loop_out_->frames_queued(loop_in_);
  EXPECT_EQ(rc, 0);

  loop_in_->close_dev(loop_in_);
  loop_out_->close_dev(loop_out_);
}

TEST_F(LoopBackTestSuite, CheckSharedBufferLimit) {
  static cras_audio_area *area;
  unsigned int nread = 1024 * 16;

  loop_out_->open_dev(loop_out_);
  loop_in_->open_dev(loop_in_);

  // Check loopback shared buffer limit.
  loop_out_->get_buffer(loop_out_, &area, &nread);
  EXPECT_EQ(nread, 8192);
  loop_out_->put_buffer(loop_out_, nread);

  loop_in_->close_dev(loop_in_);
  loop_out_->close_dev(loop_out_);
}

/* Stubs */
extern "C" {

void cras_audio_area_config_buf_pointers(struct cras_audio_area *area,
                                         const struct cras_audio_format *fmt,
                                         uint8_t *base_buffer)
{
  dummy_audio_area->channels[0].buf = base_buffer;
}

void cras_iodev_free_audio_area(struct cras_iodev *iodev)
{
}

void cras_iodev_free_format(struct cras_iodev *iodev)
{
}

void cras_iodev_init_audio_area(struct cras_iodev *iodev, int num_channels)
{
  iodev->area = dummy_audio_area;
}

int cras_iodev_list_rm_input(struct cras_iodev *input)
{
  return 0;
}

}  // extern "C"

}  //  namespace

int main(int argc, char **argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
