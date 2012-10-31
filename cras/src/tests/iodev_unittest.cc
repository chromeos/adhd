// Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stdio.h>
#include <gtest/gtest.h>

extern "C" {
#include "cras_iodev.h"
#include "cras_rstream.h"
#include "utlist.h"
}

namespace {

static struct timespec clock_gettime_retspec;

//  Test fill_time_from_frames
TEST(IoDevTestSuite, FillTimeFromFramesNormal) {
  struct timespec ts;

  cras_iodev_fill_time_from_frames(12000, 48000, &ts);
  EXPECT_EQ(0, ts.tv_sec);
  EXPECT_GE(ts.tv_nsec, 249900000);
  EXPECT_LE(ts.tv_nsec, 250100000);
}

TEST(IoDevTestSuite, FillTimeFromFramesLong) {
  struct timespec ts;

  cras_iodev_fill_time_from_frames(120000 - 12000, 48000, &ts);
  EXPECT_EQ(2, ts.tv_sec);
  EXPECT_GE(ts.tv_nsec, 249900000);
  EXPECT_LE(ts.tv_nsec, 250100000);
}

TEST(IoDevTestSuite, FillTimeFromFramesShort) {
  struct timespec ts;

  cras_iodev_fill_time_from_frames(12000 - 12000, 48000, &ts);
  EXPECT_EQ(0, ts.tv_sec);
  EXPECT_EQ(0, ts.tv_nsec);
}

//  Test set_playback_timestamp.
TEST(IoDevTestSuite, SetPlaybackTimeStampSimple) {
  struct timespec ts;

  clock_gettime_retspec.tv_sec = 1;
  clock_gettime_retspec.tv_nsec = 0;
  cras_iodev_set_playback_timestamp(48000, 24000, &ts);
  EXPECT_EQ(1, ts.tv_sec);
  EXPECT_GE(ts.tv_nsec, 499900000);
  EXPECT_LE(ts.tv_nsec, 500100000);
}

TEST(IoDevTestSuite, SetPlaybackTimeStampWrap) {
  struct timespec ts;

  clock_gettime_retspec.tv_sec = 1;
  clock_gettime_retspec.tv_nsec = 750000000;
  cras_iodev_set_playback_timestamp(48000, 24000, &ts);
  EXPECT_EQ(2, ts.tv_sec);
  EXPECT_GE(ts.tv_nsec, 249900000);
  EXPECT_LE(ts.tv_nsec, 250100000);
}

TEST(IoDevTestSuite, SetPlaybackTimeStampWrapTwice) {
  struct timespec ts;

  clock_gettime_retspec.tv_sec = 1;
  clock_gettime_retspec.tv_nsec = 750000000;
  cras_iodev_set_playback_timestamp(48000, 72000, &ts);
  EXPECT_EQ(3, ts.tv_sec);
  EXPECT_GE(ts.tv_nsec, 249900000);
  EXPECT_LE(ts.tv_nsec, 250100000);
}

//  Test set_capture_timestamp.
TEST(IoDevTestSuite, SetCaptureTimeStampSimple) {
  struct timespec ts;

  clock_gettime_retspec.tv_sec = 1;
  clock_gettime_retspec.tv_nsec = 750000000;
  cras_iodev_set_capture_timestamp(48000, 24000, &ts);
  EXPECT_EQ(1, ts.tv_sec);
  EXPECT_GE(ts.tv_nsec, 249900000);
  EXPECT_LE(ts.tv_nsec, 250100000);
}

TEST(IoDevTestSuite, SetCaptureTimeStampWrap) {
  struct timespec ts;

  clock_gettime_retspec.tv_sec = 1;
  clock_gettime_retspec.tv_nsec = 0;
  cras_iodev_set_capture_timestamp(48000, 24000, &ts);
  EXPECT_EQ(0, ts.tv_sec);
  EXPECT_GE(ts.tv_nsec, 499900000);
  EXPECT_LE(ts.tv_nsec, 500100000);
}

TEST(IoDevTestSuite, SetCaptureTimeStampWrapPartial) {
  struct timespec ts;

  clock_gettime_retspec.tv_sec = 2;
  clock_gettime_retspec.tv_nsec = 750000000;
  cras_iodev_set_capture_timestamp(48000, 72000, &ts);
  EXPECT_EQ(1, ts.tv_sec);
  EXPECT_GE(ts.tv_nsec, 249900000);
  EXPECT_LE(ts.tv_nsec, 250100000);
}

TEST(IoDevTestSuite, TestConfigParamsOneStream) {
  struct cras_iodev iodev;
  struct cras_rstream stream1;
  struct cras_io_stream iostream1;

  memset(&iodev, 0, sizeof(iodev));

  stream1.buffer_frames = 10;
  stream1.cb_threshold = 3;

  iostream1.stream = &stream1;
  DL_APPEND(iodev.streams, &iostream1);
  iodev.buffer_size = 1024;

  cras_iodev_config_params_for_streams(&iodev);
  EXPECT_EQ(iodev.used_size, 10);
  EXPECT_EQ(iodev.cb_threshold, 3);
}

TEST(IoDevTestSuite, TestConfigParamsOneStreamLimitThreshold) {
  struct cras_iodev iodev;
  struct cras_rstream stream1;
  struct cras_io_stream iostream1;

  memset(&iodev, 0, sizeof(iodev));

  stream1.buffer_frames = 10;
  stream1.cb_threshold = 10;

  iostream1.stream = &stream1;
  DL_APPEND(iodev.streams, &iostream1);
  iodev.buffer_size = 1024;

  cras_iodev_config_params_for_streams(&iodev);
  EXPECT_EQ(iodev.used_size, 10);
  EXPECT_EQ(iodev.cb_threshold, 5);

  iodev.direction = CRAS_STREAM_INPUT;
  cras_iodev_config_params_for_streams(&iodev);
  EXPECT_EQ(iodev.used_size, 10);
  EXPECT_EQ(iodev.cb_threshold, 10);
}

TEST(IoDevTestSuite, TestConfigParamsOneStreamUsedGreaterBuffer) {
  struct cras_iodev iodev;
  struct cras_rstream stream1;
  struct cras_io_stream iostream1;

  memset(&iodev, 0, sizeof(iodev));

  stream1.buffer_frames = 1280;
  stream1.cb_threshold = 1400;

  iostream1.stream = &stream1;
  DL_APPEND(iodev.streams, &iostream1);
  iodev.buffer_size = 1024;

  cras_iodev_config_params_for_streams(&iodev);
  EXPECT_EQ(iodev.used_size, 1024);
  EXPECT_EQ(iodev.cb_threshold, 512);
}

TEST(IoDevTestSuite, TestConfigParamsTwoStreamsFirstLonger) {
  struct cras_iodev iodev;
  struct cras_rstream stream1;
  struct cras_io_stream iostream1;
  struct cras_rstream stream2;
  struct cras_io_stream iostream2;

  memset(&iodev, 0, sizeof(iodev));

  stream1.buffer_frames = 10;
  stream1.cb_threshold = 3;
  stream2.buffer_frames = 8;
  stream2.cb_threshold = 5;

  iostream1.stream = &stream1;
  iostream2.stream = &stream2;
  DL_APPEND(iodev.streams, &iostream1);
  DL_APPEND(iodev.streams, &iostream2);
  iodev.buffer_size = 1024;

  cras_iodev_config_params_for_streams(&iodev);
  EXPECT_EQ(iodev.used_size, 8);
  EXPECT_EQ(iodev.cb_threshold, 4);
}

TEST(IoDevTestSuite, TestConfigParamsTwoStreamsSecondLonger) {
  struct cras_iodev iodev;
  struct cras_rstream stream1;
  struct cras_io_stream iostream1;
  struct cras_rstream stream2;
  struct cras_io_stream iostream2;

  memset(&iodev, 0, sizeof(iodev));

  stream1.buffer_frames = 10;
  stream1.cb_threshold = 3;
  stream2.buffer_frames = 80;
  stream2.cb_threshold = 5;

  iostream1.stream = &stream1;
  iostream2.stream = &stream2;
  DL_APPEND(iodev.streams, &iostream1);
  DL_APPEND(iodev.streams, &iostream2);
  iodev.buffer_size = 1024;

  cras_iodev_config_params_for_streams(&iodev);
  EXPECT_EQ(iodev.used_size, 10);
  EXPECT_EQ(iodev.cb_threshold, 3);
}

TEST(IoDevTestSuite, TestPluggedTiming) {
  struct cras_iodev iodev;
  struct timeval tv;
  struct timeval tv2;

  memset(&iodev, 0, sizeof(iodev));

  cras_iodev_plug_event(&iodev, 1);
  EXPECT_EQ(1, cras_iodev_is_plugged_in(&iodev));
  tv = cras_iodev_last_plugged_time(&iodev);
  EXPECT_GT(tv.tv_sec, 1);

  cras_iodev_plug_event(&iodev, 0);
  EXPECT_EQ(0, cras_iodev_is_plugged_in(&iodev));

  usleep(1000); // insure different times for plug events.

  cras_iodev_plug_event(&iodev, 1);
  EXPECT_EQ(1, cras_iodev_is_plugged_in(&iodev));
  tv2 = cras_iodev_last_plugged_time(&iodev);
  EXPECT_TRUE(tv2.tv_sec > tv.tv_sec ||
              (tv2.tv_sec == tv.tv_sec && tv2.tv_usec > tv.tv_usec));
}

class IoDevSetFormatTestSuite : public testing::Test {
  protected:
    virtual void SetUp() {
      sample_rates_[0] = 44100;
      sample_rates_[1] = 48000;
      sample_rates_[2] = 0;

      channel_counts_[0] = 2;
      channel_counts_[1] = 0;

      memset(&iodev_, 0, sizeof(iodev_));
      iodev_.supported_rates = sample_rates_;
      iodev_.supported_channel_counts = channel_counts_;
    }

    virtual void TearDown() {
      cras_iodev_free_format(&iodev_);
    }

    struct cras_iodev iodev_;
    size_t sample_rates_[3];
    size_t channel_counts_[2];
};

TEST_F(IoDevSetFormatTestSuite, SupportedFormatSecondary) {
  struct cras_audio_format fmt;
  int rc;

  fmt.format = SND_PCM_FORMAT_S16_LE;
  fmt.frame_rate = 48000;
  fmt.num_channels = 2;
  rc = cras_iodev_set_format(&iodev_, &fmt);
  EXPECT_EQ(0, rc);
  EXPECT_EQ(SND_PCM_FORMAT_S16_LE, fmt.format);
  EXPECT_EQ(48000, fmt.frame_rate);
  EXPECT_EQ(2, fmt.num_channels);
}

TEST_F(IoDevSetFormatTestSuite, SupportedFormatPrimary) {
  struct cras_audio_format fmt;
  int rc;

  fmt.format = SND_PCM_FORMAT_S16_LE;
  fmt.frame_rate = 44100;
  fmt.num_channels = 2;
  rc = cras_iodev_set_format(&iodev_, &fmt);
  EXPECT_EQ(0, rc);
  EXPECT_EQ(SND_PCM_FORMAT_S16_LE, fmt.format);
  EXPECT_EQ(44100, fmt.frame_rate);
  EXPECT_EQ(2, fmt.num_channels);
}

TEST_F(IoDevSetFormatTestSuite, SupportedFormatDivisor) {
  struct cras_audio_format fmt;
  int rc;

  fmt.format = SND_PCM_FORMAT_S16_LE;
  fmt.frame_rate = 96000;
  fmt.num_channels = 2;
  rc = cras_iodev_set_format(&iodev_, &fmt);
  EXPECT_EQ(0, rc);
  EXPECT_EQ(SND_PCM_FORMAT_S16_LE, fmt.format);
  EXPECT_EQ(48000, fmt.frame_rate);
  EXPECT_EQ(2, fmt.num_channels);
}

TEST_F(IoDevSetFormatTestSuite, UnsupportedChannelCount) {
  struct cras_audio_format fmt;
  int rc;

  fmt.format = SND_PCM_FORMAT_S16_LE;
  fmt.frame_rate = 96000;
  fmt.num_channels = 1;
  rc = cras_iodev_set_format(&iodev_, &fmt);
  EXPECT_EQ(0, rc);
  EXPECT_EQ(SND_PCM_FORMAT_S16_LE, fmt.format);
  EXPECT_EQ(48000, fmt.frame_rate);
  EXPECT_EQ(2, fmt.num_channels);
}

TEST_F(IoDevSetFormatTestSuite, SupportedFormatFallbackDefault) {
  struct cras_audio_format fmt;
  int rc;

  fmt.format = SND_PCM_FORMAT_S16_LE;
  fmt.frame_rate = 96008;
  fmt.num_channels = 2;
  rc = cras_iodev_set_format(&iodev_, &fmt);
  EXPECT_EQ(0, rc);
  EXPECT_EQ(SND_PCM_FORMAT_S16_LE, fmt.format);
  EXPECT_EQ(44100, fmt.frame_rate);
  EXPECT_EQ(2, fmt.num_channels);
}

extern "C" {

//  From libpthread.
int pthread_create(pthread_t *thread, const pthread_attr_t *attr,
                   void *(*start_routine)(void*), void *arg) {
  return 0;
}

int pthread_join(pthread_t thread, void **value_ptr) {
  return 0;
}

//  From librt.
int clock_gettime(clockid_t clk_id, struct timespec *tp) {
  tp->tv_sec = clock_gettime_retspec.tv_sec;
  tp->tv_nsec = clock_gettime_retspec.tv_nsec;
  return 0;
}

// From cras_system_state.
void cras_system_state_stream_added() {
}

void cras_system_state_stream_removed() {
}

// From cras_dsp
struct cras_dsp_context *cras_dsp_context_new(int channels, int sample_rate,
                                              const char *purpose)
{
  return NULL;
}

void cras_dsp_context_free(struct cras_dsp_context *ctx)
{
}

void cras_dsp_load_pipeline(struct cras_dsp_context *ctx)
{
}

// From audio thread
int audio_thread_post_message(struct audio_thread *thread,
                              struct audio_thread_msg *msg) {
  return 0;
}

}  // extern "C"
}  //  namespace

int main(int argc, char **argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
