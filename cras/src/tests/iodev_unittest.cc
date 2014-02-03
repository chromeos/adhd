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

static int select_node_called;
static enum CRAS_STREAM_DIRECTION select_node_direction;
static cras_node_id_t select_node_id;
static struct cras_ionode *node_selected;
static size_t notify_nodes_changed_called;
static size_t notify_active_node_changed_called;
static size_t notify_node_volume_called;
static size_t notify_node_capture_gain_called;
static int dsp_context_new_channels;
static int dsp_context_new_sample_rate;
static const char *dsp_context_new_purpose;
static int update_channel_layout_called;
static int update_channel_layout_return_val;

// Iodev callback
int update_channel_layout(struct cras_iodev *iodev) {
  update_channel_layout_called = 1;
  return update_channel_layout_return_val;
}

void ResetStubData() {
  select_node_called = 0;
  notify_nodes_changed_called = 0;
  notify_active_node_changed_called = 0;
  notify_node_volume_called = 0;
  notify_node_capture_gain_called = 0;
  dsp_context_new_channels = 0;
  dsp_context_new_sample_rate = 0;
  dsp_context_new_purpose = NULL;
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
  struct cras_timespec ts;

  clock_gettime_retspec.tv_sec = 1;
  clock_gettime_retspec.tv_nsec = 0;
  cras_iodev_set_playback_timestamp(48000, 24000, &ts);
  EXPECT_EQ(1, ts.tv_sec);
  EXPECT_GE(ts.tv_nsec, 499900000);
  EXPECT_LE(ts.tv_nsec, 500100000);
}

TEST(IoDevTestSuite, SetPlaybackTimeStampWrap) {
  struct cras_timespec ts;

  clock_gettime_retspec.tv_sec = 1;
  clock_gettime_retspec.tv_nsec = 750000000;
  cras_iodev_set_playback_timestamp(48000, 24000, &ts);
  EXPECT_EQ(2, ts.tv_sec);
  EXPECT_GE(ts.tv_nsec, 249900000);
  EXPECT_LE(ts.tv_nsec, 250100000);
}

TEST(IoDevTestSuite, SetPlaybackTimeStampWrapTwice) {
  struct cras_timespec ts;

  clock_gettime_retspec.tv_sec = 1;
  clock_gettime_retspec.tv_nsec = 750000000;
  cras_iodev_set_playback_timestamp(48000, 72000, &ts);
  EXPECT_EQ(3, ts.tv_sec);
  EXPECT_GE(ts.tv_nsec, 249900000);
  EXPECT_LE(ts.tv_nsec, 250100000);
}

//  Test set_capture_timestamp.
TEST(IoDevTestSuite, SetCaptureTimeStampSimple) {
  struct cras_timespec ts;

  clock_gettime_retspec.tv_sec = 1;
  clock_gettime_retspec.tv_nsec = 750000000;
  cras_iodev_set_capture_timestamp(48000, 24000, &ts);
  EXPECT_EQ(1, ts.tv_sec);
  EXPECT_GE(ts.tv_nsec, 249900000);
  EXPECT_LE(ts.tv_nsec, 250100000);
}

TEST(IoDevTestSuite, SetCaptureTimeStampWrap) {
  struct cras_timespec ts;

  clock_gettime_retspec.tv_sec = 1;
  clock_gettime_retspec.tv_nsec = 0;
  cras_iodev_set_capture_timestamp(48000, 24000, &ts);
  EXPECT_EQ(0, ts.tv_sec);
  EXPECT_GE(ts.tv_nsec, 499900000);
  EXPECT_LE(ts.tv_nsec, 500100000);
}

TEST(IoDevTestSuite, SetCaptureTimeStampWrapPartial) {
  struct cras_timespec ts;

  clock_gettime_retspec.tv_sec = 2;
  clock_gettime_retspec.tv_nsec = 750000000;
  cras_iodev_set_capture_timestamp(48000, 72000, &ts);
  EXPECT_EQ(1, ts.tv_sec);
  EXPECT_GE(ts.tv_nsec, 249900000);
  EXPECT_LE(ts.tv_nsec, 250100000);
}

TEST(IoDevTestSuite, TestConfigParamsOneStream) {
  struct cras_iodev iodev;

  memset(&iodev, 0, sizeof(iodev));

  iodev.buffer_size = 1024;

  cras_iodev_config_params(&iodev, 10, 3);
  EXPECT_EQ(iodev.used_size, 10);
  EXPECT_EQ(iodev.cb_threshold, 3);
}

TEST(IoDevTestSuite, TestConfigParamsOneStreamLimitThreshold) {
  struct cras_iodev iodev;

  memset(&iodev, 0, sizeof(iodev));

  iodev.buffer_size = 1024;

  cras_iodev_config_params(&iodev, 10, 10);
  EXPECT_EQ(iodev.used_size, 10);
  EXPECT_EQ(iodev.cb_threshold, 5);

  iodev.direction = CRAS_STREAM_INPUT;
  cras_iodev_config_params(&iodev, 10, 10);
  EXPECT_EQ(iodev.used_size, 10);
  EXPECT_EQ(iodev.cb_threshold, 10);
}

TEST(IoDevTestSuite, TestConfigParamsOneStreamUsedGreaterBuffer) {
  struct cras_iodev iodev;

  memset(&iodev, 0, sizeof(iodev));

  iodev.buffer_size = 1024;

  cras_iodev_config_params(&iodev, 1280, 1400);
  EXPECT_EQ(iodev.used_size, 1024);
  EXPECT_EQ(iodev.cb_threshold, 512);
}

class IoDevSetFormatTestSuite : public testing::Test {
  protected:
    virtual void SetUp() {
      sample_rates_[0] = 44100;
      sample_rates_[1] = 48000;
      sample_rates_[2] = 0;

      channel_counts_[0] = 2;
      channel_counts_[1] = 0;
      channel_counts_[2] = 0;

      update_channel_layout_called = 0;
      update_channel_layout_return_val = 0;

      memset(&iodev_, 0, sizeof(iodev_));
      iodev_.update_channel_layout = update_channel_layout;
      iodev_.supported_rates = sample_rates_;
      iodev_.supported_channel_counts = channel_counts_;
    }

    virtual void TearDown() {
      cras_iodev_free_format(&iodev_);
    }

    struct cras_iodev iodev_;
    size_t sample_rates_[3];
    size_t channel_counts_[3];
};

TEST_F(IoDevSetFormatTestSuite, SupportedFormatSecondary) {
  struct cras_audio_format fmt;
  int rc;

  fmt.format = SND_PCM_FORMAT_S16_LE;
  fmt.frame_rate = 48000;
  fmt.num_channels = 2;
  iodev_.direction = CRAS_STREAM_OUTPUT;
  ResetStubData();
  rc = cras_iodev_set_format(&iodev_, &fmt);
  EXPECT_EQ(0, rc);
  EXPECT_EQ(SND_PCM_FORMAT_S16_LE, fmt.format);
  EXPECT_EQ(48000, fmt.frame_rate);
  EXPECT_EQ(2, fmt.num_channels);
  EXPECT_EQ(dsp_context_new_channels, 2);
  EXPECT_EQ(dsp_context_new_sample_rate, 48000);
  EXPECT_STREQ(dsp_context_new_purpose, "playback");
}

TEST_F(IoDevSetFormatTestSuite, SupportedFormatPrimary) {
  struct cras_audio_format fmt;
  int rc;

  fmt.format = SND_PCM_FORMAT_S16_LE;
  fmt.frame_rate = 44100;
  fmt.num_channels = 2;
  iodev_.direction = CRAS_STREAM_INPUT;
  ResetStubData();
  rc = cras_iodev_set_format(&iodev_, &fmt);
  EXPECT_EQ(0, rc);
  EXPECT_EQ(SND_PCM_FORMAT_S16_LE, fmt.format);
  EXPECT_EQ(44100, fmt.frame_rate);
  EXPECT_EQ(2, fmt.num_channels);
  EXPECT_EQ(dsp_context_new_channels, 2);
  EXPECT_EQ(dsp_context_new_sample_rate, 44100);
  EXPECT_STREQ(dsp_context_new_purpose, "capture");
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

TEST_F(IoDevSetFormatTestSuite, UpdateChannelLayoutSuccess) {
  struct cras_audio_format fmt;
  int rc;

  fmt.format = SND_PCM_FORMAT_S16_LE;
  fmt.frame_rate = 48000;
  fmt.num_channels = 6;

  iodev_.supported_channel_counts[0] = 6;
  iodev_.supported_channel_counts[1] = 2;

  rc = cras_iodev_set_format(&iodev_, &fmt);
  EXPECT_EQ(0, rc);
  EXPECT_EQ(SND_PCM_FORMAT_S16_LE, fmt.format);
  EXPECT_EQ(48000, fmt.frame_rate);
  EXPECT_EQ(6, fmt.num_channels);
}

TEST_F(IoDevSetFormatTestSuite, UpdateChannelLayoutFail) {
  struct cras_audio_format fmt;
  int rc;

  fmt.format = SND_PCM_FORMAT_S16_LE;
  fmt.frame_rate = 48000;
  fmt.num_channels = 6;

  update_channel_layout_return_val = -1;
  iodev_.supported_channel_counts[0] = 6;
  iodev_.supported_channel_counts[1] = 2;

  rc = cras_iodev_set_format(&iodev_, &fmt);
  EXPECT_EQ(0, rc);
  EXPECT_EQ(SND_PCM_FORMAT_S16_LE, fmt.format);
  EXPECT_EQ(48000, fmt.frame_rate);
  EXPECT_EQ(2, fmt.num_channels);
}

// The ionode that is plugged should be chosen over unplugged.
TEST(IoNodeBetter, Plugged) {
  cras_ionode a, b;

  a.plugged = 0;
  b.plugged = 1;

  node_selected = &a;

  a.plugged_time.tv_sec = 0;
  a.plugged_time.tv_usec = 1;
  b.plugged_time.tv_sec = 0;
  b.plugged_time.tv_usec = 0;

  a.priority = 1;
  b.priority = 0;

  EXPECT_FALSE(cras_ionode_better(&a, &b));
  EXPECT_TRUE(cras_ionode_better(&b, &a));
}

// The ionode both plugged, tie should be broken by selected.
TEST(IoNodeBetter, Selected) {
  cras_ionode a, b;

  a.plugged = 1;
  b.plugged = 1;

  node_selected = &b;

  a.priority = 1;
  b.priority = 0;

  a.plugged_time.tv_sec = 0;
  a.plugged_time.tv_usec = 1;
  b.plugged_time.tv_sec = 0;
  b.plugged_time.tv_usec = 0;

  EXPECT_FALSE(cras_ionode_better(&a, &b));
  EXPECT_TRUE(cras_ionode_better(&b, &a));
}

// Two ionode both plugged and selected, tie should be broken by priority.
TEST(IoNodeBetter, Priority) {
  cras_ionode a, b;

  a.plugged = 1;
  b.plugged = 1;

  node_selected = NULL;

  a.priority = 0;
  b.priority = 1;

  a.plugged_time.tv_sec = 0;
  a.plugged_time.tv_usec = 1;
  b.plugged_time.tv_sec = 0;
  b.plugged_time.tv_usec = 0;

  EXPECT_FALSE(cras_ionode_better(&a, &b));
  EXPECT_TRUE(cras_ionode_better(&b, &a));
}

// Two ionode both plugged and have the same priority, tie should be broken
// by plugged time.
TEST(IoNodeBetter, RecentlyPlugged) {
  cras_ionode a, b;

  a.plugged = 1;
  b.plugged = 1;

  node_selected = NULL;

  a.priority = 1;
  b.priority = 1;

  a.plugged_time.tv_sec = 0;
  a.plugged_time.tv_usec = 0;
  b.plugged_time.tv_sec = 0;
  b.plugged_time.tv_usec = 1;

  EXPECT_FALSE(cras_ionode_better(&a, &b));
  EXPECT_TRUE(cras_ionode_better(&b, &a));
}

static void update_active_node(struct cras_iodev *iodev)
{
}

static void dev_set_volume(struct cras_iodev *iodev)
{
}

static void dev_set_capture_gain(struct cras_iodev *iodev)
{
}

TEST(IoNodePlug, ClearSelection) {
  struct cras_iodev iodev;
  struct cras_ionode ionode;

  memset(&iodev, 0, sizeof(iodev));
  memset(&ionode, 0, sizeof(ionode));
  ionode.dev = &iodev;
  iodev.direction = CRAS_STREAM_INPUT;
  iodev.update_active_node = update_active_node;
  ResetStubData();
  cras_iodev_set_node_attr(&ionode, IONODE_ATTR_PLUGGED, 1);
}

TEST(IoDev, AddRemoveNode) {
  struct cras_iodev iodev;
  struct cras_ionode ionode;

  memset(&iodev, 0, sizeof(iodev));
  memset(&ionode, 0, sizeof(ionode));
  ResetStubData();
  EXPECT_EQ(0, notify_nodes_changed_called);
  cras_iodev_add_node(&iodev, &ionode);
  EXPECT_EQ(1, notify_nodes_changed_called);
  cras_iodev_rm_node(&iodev, &ionode);
  EXPECT_EQ(2, notify_nodes_changed_called);
}

TEST(IoDev, SetActiveNode) {
  struct cras_iodev iodev;
  struct cras_ionode ionode;

  memset(&iodev, 0, sizeof(iodev));
  memset(&ionode, 0, sizeof(ionode));
  ResetStubData();
  EXPECT_EQ(0, notify_active_node_changed_called);
  cras_iodev_set_active_node(&iodev, &ionode);
  EXPECT_EQ(1, notify_active_node_changed_called);
}

TEST(IoDev, SetNodeVolume) {
  struct cras_iodev iodev;
  struct cras_ionode ionode;

  memset(&iodev, 0, sizeof(iodev));
  memset(&ionode, 0, sizeof(ionode));
  iodev.set_volume = dev_set_volume;
  iodev.set_capture_gain = dev_set_capture_gain;
  ionode.dev = &iodev;
  ResetStubData();
  cras_iodev_set_node_attr(&ionode, IONODE_ATTR_VOLUME, 10);
  EXPECT_EQ(1, notify_node_volume_called);
  iodev.direction = CRAS_STREAM_INPUT;
  cras_iodev_set_node_attr(&ionode, IONODE_ATTR_CAPTURE_GAIN, 10);
  EXPECT_EQ(1, notify_node_capture_gain_called);
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
  dsp_context_new_channels = channels;
  dsp_context_new_sample_rate = sample_rate;
  dsp_context_new_purpose = purpose;
  return NULL;
}

void cras_dsp_context_free(struct cras_dsp_context *ctx)
{
}

void cras_dsp_load_pipeline(struct cras_dsp_context *ctx)
{
}

void cras_dsp_set_variable(struct cras_dsp_context *ctx, const char *key,
                           const char *value)
{
}

// From audio thread
int audio_thread_post_message(struct audio_thread *thread,
                              struct audio_thread_msg *msg) {
  return 0;
}

void cras_iodev_list_select_node(enum CRAS_STREAM_DIRECTION direction,
                                 cras_node_id_t node_id)
{
  select_node_called++;
  select_node_direction = direction;
  select_node_id = node_id;
}

int cras_iodev_list_node_selected(struct cras_ionode *node)
{
  return node == node_selected;
}

void cras_iodev_list_notify_nodes_changed()
{
  notify_nodes_changed_called++;
}

void cras_iodev_list_notify_active_node_changed()
{
  notify_active_node_changed_called++;
}

void cras_iodev_list_notify_node_volume(struct cras_ionode *node)
{
	notify_node_volume_called++;
}

void cras_iodev_list_notify_node_capture_gain(struct cras_ionode *node)
{
	notify_node_capture_gain_called++;
}

}  // extern "C"
}  //  namespace

int main(int argc, char **argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
