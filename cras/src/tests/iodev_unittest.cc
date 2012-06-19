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

  cras_iodev_fill_time_from_frames(24000, 12000, 48000, &ts);
  EXPECT_EQ(0, ts.tv_sec);
  EXPECT_GE(ts.tv_nsec, 249900000);
  EXPECT_LE(ts.tv_nsec, 250100000);
}

TEST(IoDevTestSuite, FillTimeFromFramesLong) {
  struct timespec ts;

  cras_iodev_fill_time_from_frames(120000, 12000, 48000, &ts);
  EXPECT_EQ(2, ts.tv_sec);
  EXPECT_GE(ts.tv_nsec, 249900000);
  EXPECT_LE(ts.tv_nsec, 250100000);
}

TEST(IoDevTestSuite, FillTimeFromFramesShort) {
  struct timespec ts;

  cras_iodev_fill_time_from_frames(12000, 12000, 48000, &ts);
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

}  // extern "C"
}  //  namespace

int main(int argc, char **argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
