// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <gtest/gtest.h>

#include "cras/src/server/cras_bt_log.h"
#include "cras/src/server/cras_fl_media.h"
#include "cras/src/server/cras_iodev.h"
#include "cras/src/server/cras_iodev_list.h"
#include "cras/src/server/cras_lea_iodev.h"
#include "cras/src/server/cras_lea_manager.h"
#include "cras/src/tests/test_util.hh"
#include "cras_types.h"

static size_t connect_called;
static int connect_ret;
static struct cras_lea* lea_iodev_create_lea_val;
static struct cras_iodev cras_idev;
static struct cras_iodev cras_odev;
struct cras_iodev* lea_iodev_create_idev_ret;
struct cras_iodev* lea_iodev_create_odev_ret;
static size_t lea_iodev_create_called;
static size_t lea_iodev_destroy_called;
static size_t cras_iodev_set_node_plugged_called;
static int cras_iodev_set_node_plugged_value;
static size_t notify_nodes_changed_called;
static size_t floss_media_lea_host_start_audio_request_called;
static size_t floss_media_lea_host_stop_audio_request_called;
static size_t floss_media_lea_peer_start_audio_request_called;
static size_t floss_media_lea_peer_stop_audio_request_called;
static size_t floss_media_lea_set_group_volume_called;
static unsigned int floss_media_lea_set_group_volume_volume_val;
static int socket_ret;
static int audio_thread_add_events_callback_called;
static int audio_thread_add_events_callback_fd;
static thread_callback audio_thread_add_events_callback_cb;
static void* audio_thread_add_events_callback_data;
static int audio_thread_config_events_callback_called;
static enum AUDIO_THREAD_EVENTS_CB_TRIGGER
    audio_thread_config_events_callback_trigger;

void ResetStubData() {
  connect_called = 0;
  connect_ret = 0;
  lea_iodev_create_lea_val = NULL;
  lea_iodev_create_idev_ret = &cras_idev;
  lea_iodev_create_odev_ret = &cras_odev;
  lea_iodev_create_called = 0;
  lea_iodev_destroy_called = 0;
  cras_iodev_set_node_plugged_called = 0;
  cras_iodev_set_node_plugged_value = 0;
  notify_nodes_changed_called = 0;
  floss_media_lea_host_start_audio_request_called = 0;
  floss_media_lea_host_stop_audio_request_called = 0;
  floss_media_lea_peer_start_audio_request_called = 0;
  floss_media_lea_peer_stop_audio_request_called = 0;
  floss_media_lea_set_group_volume_called = 0;
  floss_media_lea_set_group_volume_volume_val = 0;
  audio_thread_add_events_callback_called = 0;
  audio_thread_add_events_callback_fd = 0;
  audio_thread_add_events_callback_cb = NULL;
  audio_thread_add_events_callback_data = NULL;
  audio_thread_config_events_callback_called = 0;
  socket_ret = 456;
}

namespace {

class LeaManagerTestSuite : public testing::Test {
 protected:
  virtual void SetUp() {
    ResetStubData();
    btlog = cras_bt_event_log_init();
  }

  virtual void TearDown() { cras_bt_event_log_deinit(btlog); }
};

TEST_F(LeaManagerTestSuite, PCMCreateDestroy) {
  struct cras_lea* lea = cras_floss_lea_create(NULL);
  ASSERT_NE(lea, (struct cras_lea*)NULL);

  {
    CLEAR_AND_EVENTUALLY(EXPECT_EQ, lea_iodev_create_lea_val, lea);
    CLEAR_AND_EVENTUALLY(EXPECT_EQ, lea_iodev_create_called, 2);
    CLEAR_AND_EVENTUALLY(EXPECT_EQ, cras_iodev_set_node_plugged_called, 2);
    CLEAR_AND_EVENTUALLY(EXPECT_EQ, notify_nodes_changed_called, 1);
    cras_floss_lea_add_group(lea, "name", 0);
  }

  {
    CLEAR_AND_EVENTUALLY(EXPECT_EQ, cras_iodev_set_node_plugged_value, 0);
    CLEAR_AND_EVENTUALLY(EXPECT_EQ, cras_iodev_set_node_plugged_called, 2);
    CLEAR_AND_EVENTUALLY(EXPECT_EQ, lea_iodev_destroy_called, 2);
    cras_floss_lea_remove_group(lea, 0);
  }

  cras_floss_lea_destroy(lea);
}

TEST_F(LeaManagerTestSuite, StartWithSocketFail) {
  struct cras_lea* lea = cras_floss_lea_create(NULL);
  ASSERT_NE(lea, (struct cras_lea*)NULL);

  cras_floss_lea_add_group(lea, "name", 0);

  socket_ret = -1;

  thread_callback rwcb = reinterpret_cast<thread_callback>(0xdeadbeef);
  EXPECT_EQ(socket_ret, cras_floss_lea_start(lea, rwcb, CRAS_STREAM_OUTPUT));

  EXPECT_EQ(floss_media_lea_host_start_audio_request_called, 1);
  EXPECT_EQ(audio_thread_add_events_callback_called, 0);
  EXPECT_EQ(floss_media_lea_host_stop_audio_request_called, 1);
  EXPECT_EQ(connect_called, 0);
  EXPECT_EQ(cras_floss_lea_get_fd(lea), -1);

  cras_floss_lea_remove_group(lea, 0);
  EXPECT_EQ(lea_iodev_destroy_called, 2);

  cras_floss_lea_destroy(lea);
}

TEST_F(LeaManagerTestSuite, StartWithConnectFail) {
  struct cras_lea* lea = cras_floss_lea_create(NULL);
  ASSERT_NE(lea, (struct cras_lea*)NULL);

  cras_floss_lea_add_group(lea, "name", 0);

  connect_ret = -1;

  thread_callback rwcb = reinterpret_cast<thread_callback>(0xdeadbeef);
  EXPECT_EQ(connect_ret, cras_floss_lea_start(lea, rwcb, CRAS_STREAM_OUTPUT));

  EXPECT_EQ(floss_media_lea_host_start_audio_request_called, 1);
  EXPECT_EQ(connect_called, 1);
  EXPECT_EQ(audio_thread_add_events_callback_called, 0);
  EXPECT_EQ(floss_media_lea_host_stop_audio_request_called, 1);
  EXPECT_EQ(cras_floss_lea_get_fd(lea), -1);

  cras_floss_lea_remove_group(lea, 0);
  EXPECT_EQ(lea_iodev_destroy_called, 2);

  cras_floss_lea_destroy(lea);
}

TEST_F(LeaManagerTestSuite, StartStop) {
  struct cras_lea* lea = cras_floss_lea_create(NULL);
  ASSERT_NE(lea, (struct cras_lea*)NULL);

  cras_floss_lea_add_group(lea, "name", 0);

  EXPECT_EQ(cras_floss_lea_get_fd(lea), -1);

  thread_callback rwcb = reinterpret_cast<thread_callback>(0xdeadbeef);
  cras_floss_lea_start(lea, rwcb, CRAS_STREAM_OUTPUT);
  EXPECT_EQ(floss_media_lea_host_start_audio_request_called, 1);
  EXPECT_EQ(cras_floss_lea_get_fd(lea), socket_ret);

  cras_floss_lea_start(lea, rwcb, CRAS_STREAM_INPUT);
  EXPECT_EQ(floss_media_lea_peer_start_audio_request_called, 1);
  EXPECT_EQ(audio_thread_add_events_callback_called, 1);
  EXPECT_EQ(audio_thread_add_events_callback_fd, socket_ret);
  EXPECT_EQ((struct cras_lea*)audio_thread_add_events_callback_data, lea);

  cras_floss_lea_stop(lea, CRAS_STREAM_OUTPUT);
  EXPECT_EQ(floss_media_lea_host_stop_audio_request_called, 1);
  EXPECT_EQ(cras_floss_lea_get_fd(lea), socket_ret);

  cras_floss_lea_stop(lea, CRAS_STREAM_INPUT);
  EXPECT_EQ(floss_media_lea_peer_stop_audio_request_called, 1);
  EXPECT_EQ(cras_floss_lea_get_fd(lea), -1);

  cras_floss_lea_remove_group(lea, 0);
  EXPECT_EQ(lea_iodev_destroy_called, 2);

  cras_floss_lea_destroy(lea);
}

TEST_F(LeaManagerTestSuite, SetVolume) {
  struct cras_lea* lea = cras_floss_lea_create(NULL);
  ASSERT_NE(lea, (struct cras_lea*)NULL);

  cras_floss_lea_add_group(lea, "name", 0);
  EXPECT_EQ(lea, lea_iodev_create_lea_val);
  EXPECT_EQ(lea_iodev_create_called, 2);

  cras_floss_lea_set_volume(lea, 100);
  EXPECT_EQ(floss_media_lea_set_group_volume_called, 1);
  EXPECT_EQ(floss_media_lea_set_group_volume_volume_val, 255);

  cras_floss_lea_set_volume(lea, 0);
  EXPECT_EQ(floss_media_lea_set_group_volume_called, 2);
  EXPECT_EQ(floss_media_lea_set_group_volume_volume_val, 0);

  cras_floss_lea_set_volume(lea, 50);
  EXPECT_EQ(floss_media_lea_set_group_volume_called, 3);
  EXPECT_EQ(floss_media_lea_set_group_volume_volume_val, 127);

  cras_floss_lea_set_volume(lea, 20);
  EXPECT_EQ(floss_media_lea_set_group_volume_called, 4);
  EXPECT_EQ(floss_media_lea_set_group_volume_volume_val, 51);

  cras_floss_lea_remove_group(lea, 0);
  EXPECT_EQ(lea_iodev_destroy_called, 2);

  cras_floss_lea_destroy(lea);
}
}  // namespace

extern "C" {

// socket and connect
int socket(int domain, int type, int protocol) {
  return socket_ret;
}

int connect(int sockfd, const struct sockaddr* addr, socklen_t addrlen) {
  connect_called++;
  return connect_ret;
}

// From audio_thread
struct audio_thread_event_log* atlog;

// From cras_bt_log
struct cras_bt_event_log* btlog;

void audio_thread_add_events_callback(int fd,
                                      thread_callback cb,
                                      void* data,
                                      int events) {
  audio_thread_add_events_callback_called++;
  audio_thread_add_events_callback_fd = fd;
  audio_thread_add_events_callback_cb = cb;
  audio_thread_add_events_callback_data = data;
}

void audio_thread_config_events_callback(
    int fd,
    enum AUDIO_THREAD_EVENTS_CB_TRIGGER trigger) {
  audio_thread_config_events_callback_called++;
  audio_thread_config_events_callback_trigger = trigger;
}

int audio_thread_rm_callback_sync(struct audio_thread* thread, int fd) {
  return 0;
}

// From cras iodev list
struct audio_thread* cras_iodev_list_get_audio_thread() {
  return NULL;
}

void cras_iodev_set_node_plugged(struct cras_ionode* ionode, int plugged) {
  cras_iodev_set_node_plugged_called++;
  cras_iodev_set_node_plugged_value = plugged;
}

// From cras iodev
void cras_iodev_list_notify_nodes_changed() {
  notify_nodes_changed_called++;
}

// From cras_lea_iodev
struct cras_iodev* lea_iodev_create(struct cras_lea* lea,
                                    const char* name,
                                    int group_id,
                                    enum CRAS_STREAM_DIRECTION dir) {
  lea_iodev_create_lea_val = lea;
  lea_iodev_create_called++;

  switch (dir) {
    case CRAS_STREAM_OUTPUT:
      return lea_iodev_create_odev_ret;
    case CRAS_STREAM_INPUT:
      return lea_iodev_create_idev_ret;
    default:
      return NULL;
  }

  return NULL;
}

void lea_iodev_destroy(struct cras_iodev* iodev) {
  lea_iodev_destroy_called++;
  return;
}

// From cras_fl_media
int floss_media_lea_host_start_audio_request(struct fl_media* fm,
                                             uint32_t* data_interval_us,
                                             uint32_t* sample_rate,
                                             uint8_t* bits_per_sample,
                                             uint8_t* channels_count) {
  floss_media_lea_host_start_audio_request_called++;
  return 0;
}

int floss_media_lea_peer_start_audio_request(struct fl_media* fm,
                                             uint32_t* data_interval_us,
                                             uint32_t* sample_rate,
                                             uint8_t* bits_per_sample,
                                             uint8_t* channels_count) {
  floss_media_lea_peer_start_audio_request_called++;
  return 0;
}

int floss_media_lea_host_stop_audio_request(struct fl_media* fm) {
  floss_media_lea_host_stop_audio_request_called++;
  return 0;
}

int floss_media_lea_peer_stop_audio_request(struct fl_media* fm) {
  floss_media_lea_peer_stop_audio_request_called++;
  return 0;
}

int floss_media_lea_set_group_volume(struct fl_media* fm,
                                     int group_id,
                                     uint8_t volume) {
  floss_media_lea_set_group_volume_called++;
  floss_media_lea_set_group_volume_volume_val = volume;
  return 0;
}
}  // extern "C"
