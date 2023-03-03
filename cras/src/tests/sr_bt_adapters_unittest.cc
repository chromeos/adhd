/* Copyright 2022 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <gtest/gtest.h>

#include "cras/src/tests/sr_stub.h"

extern "C" {
#include "cras/src/server/cras_audio_area.h"
#include "cras/src/server/cras_iodev.h"
#include "cras/src/server/cras_sr_bt_adapters.h"
}

extern "C" {

// fake iodev

static struct cras_iodev fake_iodev;
static int fake_frames_queued_return_val = 0;
static int fake_frames_queued_called = 0;
static int fake_delay_frames_return_val = 0;
static int fake_delay_frames_called = 0;
static int fake_get_buffer_called = 0;
static int fake_put_buffer_called = 0;
static int fake_put_buffer_called_with_nread = 0;
static int fake_flush_buffer_called = 0;
static int16_t fake_data[3]{};
static cras_channel_area fake_channel{};
static cras_audio_area* fake_area = nullptr;
static struct timespec fake_time {};

static int fake_frames_queued(const struct cras_iodev* iodev,
                              struct timespec* tstamp) {
  ++fake_frames_queued_called;
  *tstamp = fake_time;
  return fake_frames_queued_return_val;
}

static int fake_delay_frames(const struct cras_iodev* iodev) {
  ++fake_delay_frames_called;
  return fake_delay_frames_return_val;
}

int fake_get_buffer(struct cras_iodev* iodev,
                    struct cras_audio_area** area,
                    unsigned* frames) {
  ++fake_get_buffer_called;
  fake_area->frames = MIN(*frames, fake_area->frames);
  *frames = fake_area->frames;
  *area = fake_area;
  iodev->area = *area;
  return 0;
};

int fake_put_buffer(struct cras_iodev* iodev, const unsigned nread) {
  ++fake_put_buffer_called;
  fake_frames_queued_return_val -= nread;
  fake_put_buffer_called_with_nread = nread;
  return 0;
};

int fake_flush_buffer(struct cras_iodev* iodev) {
  ++fake_flush_buffer_called;
  return 0;
};

void ResetFakeState() {
  fake_iodev.frames_queued = fake_frames_queued;
  fake_iodev.delay_frames = fake_delay_frames;
  fake_iodev.get_buffer = fake_get_buffer;
  fake_iodev.put_buffer = fake_put_buffer;
  fake_iodev.flush_buffer = fake_flush_buffer;
  fake_frames_queued_return_val = 0;
  fake_frames_queued_called = 0;
  fake_delay_frames_return_val = 0;
  fake_delay_frames_called = 0;
  fake_get_buffer_called = 0;
  fake_put_buffer_called = 0;
  fake_put_buffer_called_with_nread = 0;
  fake_flush_buffer_called = 0;
  memset(fake_data, 0, sizeof(fake_data));
  fake_channel = {
      .ch_set = 0, .step_bytes = sizeof(int16_t), .buf = (uint8_t*)fake_data};
  fake_area = nullptr;
  fake_time = {};
}

}  // extern "C"

namespace {

class SrBtAdaptersTest : public testing::Test {
 protected:
  virtual void SetUp() {
    ResetFakeState();

    fake_area = cras_audio_area_create(1);
    fake_area->frames = sizeof(fake_data) / sizeof(fake_data[0]);
    fake_area->num_channels = 1;
    fake_area->channels[0] = fake_channel;
    fake_iodev.area = fake_area;

    sr_ = cras_sr_create(cras_sr_bt_get_model_spec(SR_BT_NBS), 28800);
    adapter_ = cras_iodev_sr_bt_adapter_create(&fake_iodev, sr_);
  }

  virtual void TearDown() {
    cras_iodev_sr_bt_adapter_destroy(adapter_);
    cras_sr_destroy(sr_);
    free(fake_area);
  }

 protected:
  struct cras_sr* sr_ = nullptr;
  struct cras_iodev_sr_bt_adapter* adapter_ = nullptr;
};

TEST_F(SrBtAdaptersTest, FramesQueued) {
  cras_sr_set_frames_ratio(sr_, 3);
  fake_frames_queued_return_val = 3;
  struct timespec tstamp {
    0
  };

  const int frames_queued =
      cras_iodev_sr_bt_adapter_frames_queued(adapter_, &tstamp);

  EXPECT_EQ(9, frames_queued);
  EXPECT_LE(1, fake_frames_queued_called);
  EXPECT_EQ(1, fake_get_buffer_called);
  EXPECT_EQ(1, fake_put_buffer_called);
  EXPECT_EQ(3, fake_put_buffer_called_with_nread);
}

TEST_F(SrBtAdaptersTest, DelayFrames) {
  cras_sr_set_frames_ratio(sr_, 3);
  fake_delay_frames_return_val = 3;

  const int delay_frames = cras_iodev_sr_bt_adapter_delay_frames(adapter_);

  EXPECT_EQ(9, delay_frames);
  EXPECT_EQ(1, fake_delay_frames_called);
}

TEST_F(SrBtAdaptersTest, FramesQueuedMoreThanNumFramesPerRunMs) {
  cras_sr_set_frames_ratio(sr_, 3);
  cras_sr_set_num_frames_per_run(sr_, 9);
  fake_frames_queued_return_val = 4;  // 9 / 3 + 1
  struct timespec tstamp {
    0
  };

  {  // 1st frames_queued
    const int framed_queued =
        cras_iodev_sr_bt_adapter_frames_queued(adapter_, &tstamp);

    EXPECT_EQ(12, framed_queued);
    EXPECT_LE(1, fake_frames_queued_called);
    EXPECT_EQ(1, fake_get_buffer_called);
    EXPECT_EQ(1, fake_put_buffer_called);
    EXPECT_EQ(3, fake_put_buffer_called_with_nread);
  }

  {  // 1st get_buffer
    unsigned frames = 12;
    struct cras_audio_area* area_ptr = fake_area;
    const int rc =
        cras_iodev_sr_bt_adapter_get_buffer(adapter_, &area_ptr, &frames);

    EXPECT_EQ(0, rc);
    EXPECT_EQ(9, frames);
    EXPECT_EQ(9, area_ptr->frames);
    EXPECT_EQ(1, area_ptr->num_channels);
  }

  {  // 1st put_buffer
    const int rc = cras_iodev_sr_bt_adapter_put_buffer(adapter_, 9);
    EXPECT_EQ(0, rc);
  }

  {  // 2nd frames_queued
    fake_time.tv_nsec = 5000000;
    const int framed_queued =
        cras_iodev_sr_bt_adapter_frames_queued(adapter_, &tstamp);

    EXPECT_EQ(3, framed_queued);
    EXPECT_EQ(2, fake_get_buffer_called);
    EXPECT_EQ(2, fake_put_buffer_called);
    EXPECT_EQ(1, fake_put_buffer_called_with_nread);
  }

  {  // 2nd get_buffer
    unsigned frames = 3;
    struct cras_audio_area* area_ptr = fake_area;
    const int rc =
        cras_iodev_sr_bt_adapter_get_buffer(adapter_, &area_ptr, &frames);

    EXPECT_EQ(0, rc);
    EXPECT_EQ(3, frames);
    EXPECT_EQ(3, area_ptr->frames);
    EXPECT_EQ(1, area_ptr->num_channels);
  }

  {  // 2nd put_buffer
    const int rc = cras_iodev_sr_bt_adapter_put_buffer(adapter_, 3);
    EXPECT_EQ(0, rc);
  }
}

TEST_F(SrBtAdaptersTest, FlushBuffer) {
  cras_sr_set_frames_ratio(sr_, 3);
  fake_frames_queued_return_val = 3;  // 9 / 3 + 1
  struct timespec tstamp {
    0
  };

  // populates the internal buffers.
  const int framed_queued =
      cras_iodev_sr_bt_adapter_frames_queued(adapter_, &tstamp);

  ASSERT_LT(0, framed_queued);
  ASSERT_EQ(0, fake_frames_queued_return_val);

  // flushes buffer
  cras_iodev_sr_bt_adapter_flush_buffer(adapter_);
  EXPECT_EQ(1, fake_flush_buffer_called);

  EXPECT_EQ(0, cras_iodev_sr_bt_adapter_frames_queued(adapter_, &tstamp));
}

}  // namespace
