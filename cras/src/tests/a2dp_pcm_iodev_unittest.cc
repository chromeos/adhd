// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <gtest/gtest.h>
#include <stdint.h>
#include <stdio.h>

extern "C" {
// To test static functions.
#include "cras_a2dp_pcm_iodev.c"

#include "audio_thread.h"
#include "audio_thread_log.h"
#include "cras_audio_area.h"
#include "cras_iodev.h"
#include "cras_iodev_list.h"
#include "utlist.h"
}

#define FAKE_SOCKET_FD 99;

static cras_audio_format format;

static unsigned cras_iodev_add_node_called;
static unsigned cras_iodev_rm_node_called;
static unsigned cras_iodev_set_active_node_called;
static unsigned cras_iodev_free_format_called;
static unsigned cras_iodev_free_resources_called;
static unsigned cras_iodev_list_add_output_called;
static unsigned cras_iodev_list_rm_output_called;
static cras_audio_area* mock_audio_area;
static unsigned cras_iodev_init_audio_area_called;
static unsigned cras_iodev_free_audio_area_called;
static unsigned cras_floss_a2dp_start_called;
static unsigned cras_floss_a2dp_stop_called;
static unsigned cras_a2dp_cancel_suspend_called;
static unsigned cras_a2dp_schedule_suspend_called;
static thread_callback write_callback;
static void* write_callback_data;
static int audio_thread_config_events_callback_called;
static enum AUDIO_THREAD_EVENTS_CB_TRIGGER
    audio_thread_config_events_callback_trigger;
static int cras_floss_a2dp_fill_format_called;

void ResetStubData() {
  cras_iodev_add_node_called = 0;
  cras_iodev_rm_node_called = 0;
  cras_iodev_set_active_node_called = 0;
  cras_iodev_free_format_called = 0;
  cras_iodev_free_resources_called = 0;
  cras_iodev_list_add_output_called = 0;
  cras_iodev_list_rm_output_called = 0;
  cras_iodev_init_audio_area_called = 0;
  cras_iodev_free_audio_area_called = 0;
  cras_floss_a2dp_start_called = 0;
  cras_floss_a2dp_stop_called = 0;
  cras_a2dp_cancel_suspend_called = 0;
  cras_a2dp_schedule_suspend_called = 0;
  write_callback = NULL;
  audio_thread_config_events_callback_called = 0;
  audio_thread_config_events_callback_trigger = TRIGGER_NONE;
  cras_floss_a2dp_fill_format_called = 0;
  if (!mock_audio_area) {
    mock_audio_area = (cras_audio_area*)calloc(
        1, sizeof(*mock_audio_area) + sizeof(cras_channel_area) * 2);
  }
}

int iodev_set_format(struct cras_iodev* iodev, struct cras_audio_format* fmt) {
  fmt->format = SND_PCM_FORMAT_S16_LE;
  fmt->num_channels = 2;
  fmt->frame_rate = 48000;
  iodev->format = fmt;
  return 0;
}

namespace {
class A2dpPcmIodev : public testing::Test {
 protected:
  virtual void SetUp() {
    ResetStubData();
    atlog = (audio_thread_event_log*)calloc(1, sizeof(audio_thread_event_log));
  }

  virtual void TearDown() { free(atlog); }
};

TEST_F(A2dpPcmIodev, CreateDestroyA2dpPcmIodev) {
  struct cras_iodev* iodev;

  iodev = a2dp_pcm_iodev_create(NULL, 0, 0, 0);

  EXPECT_NE(iodev, (void*)NULL);
  EXPECT_EQ(iodev->direction, CRAS_STREAM_OUTPUT);

  EXPECT_EQ(1, cras_iodev_add_node_called);
  EXPECT_EQ(1, cras_iodev_set_active_node_called);
  EXPECT_EQ(1, cras_iodev_list_add_output_called);
  EXPECT_EQ(1, cras_floss_a2dp_fill_format_called);

  a2dp_pcm_iodev_destroy(iodev);

  EXPECT_EQ(1, cras_iodev_rm_node_called);
  EXPECT_EQ(1, cras_iodev_list_rm_output_called);
  EXPECT_EQ(1, cras_iodev_free_resources_called);
}

TEST_F(A2dpPcmIodev, OpenCloseIodev) {
  struct cras_iodev* iodev;

  iodev = a2dp_pcm_iodev_create(NULL, 0, 0, 0);

  iodev_set_format(iodev, &format);
  iodev->configure_dev(iodev);
  iodev->start(iodev);
  iodev->state = CRAS_IODEV_STATE_NORMAL_RUN;

  EXPECT_EQ(1, cras_floss_a2dp_start_called);
  EXPECT_EQ(1, cras_iodev_init_audio_area_called);
  EXPECT_NE(write_callback, (void*)NULL);
  EXPECT_EQ(1, audio_thread_config_events_callback_called);
  EXPECT_EQ(TRIGGER_NONE, audio_thread_config_events_callback_trigger);

  iodev->close_dev(iodev);
  EXPECT_EQ(1, cras_floss_a2dp_stop_called);
  EXPECT_EQ(1, cras_a2dp_cancel_suspend_called);
  EXPECT_EQ(1, cras_iodev_free_format_called);
  EXPECT_EQ(1, cras_iodev_free_audio_area_called);

  a2dp_pcm_iodev_destroy(iodev);
}

}  // namespace

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}

extern "C" {
// Cras iodev
void cras_iodev_add_node(struct cras_iodev* iodev, struct cras_ionode* node) {
  cras_iodev_add_node_called++;
  iodev->nodes = node;
}

void cras_iodev_rm_node(struct cras_iodev* iodev, struct cras_ionode* node) {
  cras_iodev_rm_node_called++;
  iodev->nodes = NULL;
}

void cras_iodev_set_active_node(struct cras_iodev* iodev,
                                struct cras_ionode* node) {
  cras_iodev_set_active_node_called++;
  iodev->active_node = node;
}

void cras_iodev_free_format(struct cras_iodev* iodev) {
  cras_iodev_free_format_called++;
}

void cras_iodev_free_resources(struct cras_iodev* iodev) {
  cras_iodev_free_resources_called++;
}

void cras_iodev_init_audio_area(struct cras_iodev* iodev, int num_channels) {
  cras_iodev_init_audio_area_called++;
  iodev->area = mock_audio_area;
}

void cras_iodev_free_audio_area(struct cras_iodev* iodev) {
  cras_iodev_free_audio_area_called++;
}

void cras_audio_area_config_buf_pointers(struct cras_audio_area* area,
                                         const struct cras_audio_format* fmt,
                                         uint8_t* base_buffer) {
  mock_audio_area->channels[0].buf = base_buffer;
}

int cras_iodev_fill_odev_zeros(struct cras_iodev* odev, unsigned int frames) {
  return 0;
}

// Cras iodev list
int cras_iodev_list_add_output(struct cras_iodev* output) {
  cras_iodev_list_add_output_called++;
  return 0;
}

int cras_iodev_list_rm_output(struct cras_iodev* output) {
  cras_iodev_list_rm_output_called++;
  return 0;
}

struct audio_thread* cras_iodev_list_get_audio_thread() {
  return NULL;
}

int audio_thread_rm_callback_sync(struct audio_thread* thread, int fd) {
  return 0;
}

// From ewma_power
void ewma_power_disable(struct ewma_power* ewma) {}

// From audio_thread
struct audio_thread_event_log* atlog;

void audio_thread_add_events_callback(int fd,
                                      thread_callback cb,
                                      void* data,
                                      int events) {
  write_callback = cb;
  write_callback_data = data;
}

void audio_thread_config_events_callback(
    int fd,
    enum AUDIO_THREAD_EVENTS_CB_TRIGGER trigger) {
  audio_thread_config_events_callback_called++;
  audio_thread_config_events_callback_trigger = trigger;
}

// A2DP manager
const char* cras_floss_a2dp_get_display_name(struct cras_a2dp* a2dp) {
  return "display_name";
}

const char* cras_floss_a2dp_get_addr(struct cras_a2dp* a2dp) {
  return "11:22:33:44:55:66";
}

int cras_floss_a2dp_fill_format(int sample_rate,
                                int bits_per_sample,
                                int channel_mode,
                                size_t** rates,
                                snd_pcm_format_t** formats,
                                size_t** channel_counts) {
  cras_floss_a2dp_fill_format_called += 1;
  *rates = (size_t*)malloc(sizeof(**rates));
  *formats = (snd_pcm_format_t*)malloc(sizeof(**formats));
  *channel_counts = (size_t*)malloc(sizeof(**channel_counts));
  return 0;
}

int cras_floss_a2dp_start(struct cras_a2dp* a2dp,
                          struct cras_audio_format* fmt,
                          int* skt) {
  cras_floss_a2dp_start_called++;
  *skt = FAKE_SOCKET_FD;
  return 0;
}

int cras_floss_a2dp_stop(struct cras_a2dp* a2dp) {
  cras_floss_a2dp_stop_called++;
  return 0;
}

void cras_floss_a2dp_set_volume(struct cras_a2dp* a2dp, unsigned int volume) {
  return;
}

void cras_a2dp_cancel_suspend() {
  cras_a2dp_cancel_suspend_called++;
}

void cras_a2dp_schedule_suspend(unsigned int msec) {
  cras_a2dp_schedule_suspend_called++;
}

int cras_audio_thread_event_a2dp_throttle() {
  return 0;
}

int cras_audio_thread_event_a2dp_overrun() {
  return 0;
}
}
