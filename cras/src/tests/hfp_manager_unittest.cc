// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <gtest/gtest.h>

#include "cras_types.h"

extern "C" {
#include "cras/src/server/cras_bt_log.h"
#include "cras/src/server/cras_features_override.h"
#include "cras/src/server/cras_hfp_alsa_iodev.h"
#include "cras/src/server/cras_hfp_manager.h"
#include "cras/src/server/cras_iodev.h"
#include "cras/src/server/cras_iodev_list.h"
}

static cras_iodev* cras_iodev_list_get_sco_pcm_iodev_ret;
static size_t connect_called;
static int connect_ret;
static struct cras_hfp* hfp_pcm_iodev_create_hfp_val;
static struct cras_hfp* hfp_alsa_iodev_create_hfp_val;
struct cras_iodev* hfp_pcm_iodev_create_ret;
struct cras_iodev* hfp_alsa_iodev_create_ret;
static size_t hfp_pcm_iodev_create_called;
static size_t hfp_alsa_iodev_create_called;
static size_t hfp_pcm_iodev_destroy_called;
static size_t hfp_alsa_iodev_destroy_called;
static size_t floss_media_hfp_start_sco_called;
static size_t floss_media_hfp_stop_sco_called;
static int socket_ret;
static int audio_thread_add_events_callback_called;
static int audio_thread_add_events_callback_fd;
static thread_callback audio_thread_add_events_callback_cb;
static void* audio_thread_add_events_callback_data;
static int audio_thread_config_events_callback_called;
static enum AUDIO_THREAD_EVENTS_CB_TRIGGER
    audio_thread_config_events_callback_trigger;

void ResetStubData() {
  cras_iodev_list_get_sco_pcm_iodev_ret = NULL;

  connect_called = 0;
  connect_ret = 0;
  hfp_pcm_iodev_create_hfp_val = NULL;
  hfp_alsa_iodev_create_hfp_val = NULL;
  hfp_pcm_iodev_create_ret = reinterpret_cast<struct cras_iodev*>(0x123);
  hfp_alsa_iodev_create_ret = reinterpret_cast<struct cras_iodev*>(0x123);
  hfp_pcm_iodev_create_called = 0;
  hfp_alsa_iodev_create_called = 0;
  hfp_pcm_iodev_destroy_called = 0;
  hfp_alsa_iodev_destroy_called = 0;
  floss_media_hfp_start_sco_called = 0;
  floss_media_hfp_stop_sco_called = 0;
  audio_thread_add_events_callback_called = 0;
  audio_thread_add_events_callback_fd = 0;
  audio_thread_add_events_callback_cb = NULL;
  audio_thread_add_events_callback_data = NULL;
  audio_thread_config_events_callback_called = 0;
  socket_ret = 456;
  btlog = cras_bt_event_log_init();
}

namespace {

class HfpManagerTestSuite : public testing::Test {
 protected:
  virtual void SetUp() {
    cras_features_set_override(CrOSLateBootAudioHFPOffload, true);
    ResetStubData();
  }

  virtual void TearDown() {
    cras_bt_event_log_deinit(btlog);
    cras_features_unset_override(CrOSLateBootAudioHFPOffload);
  }
};

TEST_F(HfpManagerTestSuite, PCMCreateFailed) {
  hfp_pcm_iodev_create_ret = NULL;
  // Failing to create hfp_pcm_iodev should fail the hfp_create
  ASSERT_EQ(cras_floss_hfp_create(NULL, "addr", "name", false),
            (struct cras_hfp*)NULL);
}

TEST_F(HfpManagerTestSuite, AlsaCreateFailed) {
  cras_iodev_list_get_sco_pcm_iodev_ret =
      reinterpret_cast<struct cras_iodev*>(0xabc);

  hfp_alsa_iodev_create_ret = NULL;
  ASSERT_EQ(cras_floss_hfp_create(NULL, "addr", "name", false),
            (struct cras_hfp*)NULL);
}

TEST_F(HfpManagerTestSuite, PCMCreateDestroy) {
  struct cras_hfp* hfp = cras_floss_hfp_create(NULL, "addr", "name", false);
  ASSERT_NE(hfp, (struct cras_hfp*)NULL);
  EXPECT_EQ(hfp, hfp_pcm_iodev_create_hfp_val);
  EXPECT_EQ(hfp_pcm_iodev_create_called, 2);
  EXPECT_EQ(strncmp("name", cras_floss_hfp_get_display_name(hfp), 4), 0);

  cras_floss_hfp_destroy(hfp);
  EXPECT_EQ(hfp_pcm_iodev_destroy_called, 2);
}

TEST_F(HfpManagerTestSuite, AlsaCreateDestroy) {
  cras_iodev_list_get_sco_pcm_iodev_ret =
      reinterpret_cast<struct cras_iodev*>(0xabc);

  struct cras_hfp* hfp = cras_floss_hfp_create(NULL, "addr", "name", false);
  ASSERT_NE(hfp, (struct cras_hfp*)NULL);
  EXPECT_EQ(hfp, hfp_alsa_iodev_create_hfp_val);
  EXPECT_EQ(hfp_alsa_iodev_create_called, 2);
  EXPECT_EQ(strncmp("name", cras_floss_hfp_get_display_name(hfp), 4), 0);

  cras_floss_hfp_destroy(hfp);
  EXPECT_EQ(hfp_alsa_iodev_destroy_called, 2);
}

TEST_F(HfpManagerTestSuite, StartWithSocketFail) {
  struct cras_hfp* hfp = cras_floss_hfp_create(NULL, "addr", "name", false);
  ASSERT_NE(hfp, (struct cras_hfp*)NULL);

  socket_ret = -1;

  thread_callback rwcb = reinterpret_cast<thread_callback>(0xdeadbeef);
  EXPECT_EQ(socket_ret, cras_floss_hfp_start(hfp, rwcb, CRAS_STREAM_OUTPUT));

  EXPECT_EQ(floss_media_hfp_start_sco_called, 1);
  EXPECT_EQ(audio_thread_add_events_callback_called, 0);
  EXPECT_EQ(floss_media_hfp_stop_sco_called, 1);
  EXPECT_EQ(connect_called, 0);
  EXPECT_EQ(cras_floss_hfp_get_fd(hfp), -1);

  cras_floss_hfp_destroy(hfp);
}

TEST_F(HfpManagerTestSuite, StartWithConnectFail) {
  struct cras_hfp* hfp = cras_floss_hfp_create(NULL, "addr", "name", false);
  ASSERT_NE(hfp, (struct cras_hfp*)NULL);

  connect_ret = -1;

  thread_callback rwcb = reinterpret_cast<thread_callback>(0xdeadbeef);
  EXPECT_EQ(connect_ret, cras_floss_hfp_start(hfp, rwcb, CRAS_STREAM_OUTPUT));

  EXPECT_EQ(floss_media_hfp_start_sco_called, 1);
  EXPECT_EQ(connect_called, 1);
  EXPECT_EQ(audio_thread_add_events_callback_called, 0);
  EXPECT_EQ(floss_media_hfp_stop_sco_called, 1);
  EXPECT_EQ(cras_floss_hfp_get_fd(hfp), -1);

  cras_floss_hfp_destroy(hfp);
}

TEST_F(HfpManagerTestSuite, StartStop) {
  struct cras_hfp* hfp = cras_floss_hfp_create(NULL, "addr", "name", false);
  ASSERT_NE(hfp, (struct cras_hfp*)NULL);

  EXPECT_EQ(cras_floss_hfp_get_fd(hfp), -1);

  thread_callback rwcb = reinterpret_cast<thread_callback>(0xdeadbeef);
  cras_floss_hfp_start(hfp, rwcb, CRAS_STREAM_OUTPUT);
  EXPECT_EQ(floss_media_hfp_start_sco_called, 1);
  EXPECT_EQ(cras_floss_hfp_get_fd(hfp), socket_ret);

  cras_floss_hfp_start(hfp, rwcb, CRAS_STREAM_INPUT);
  EXPECT_EQ(floss_media_hfp_start_sco_called, 1);
  EXPECT_EQ(audio_thread_add_events_callback_called, 1);
  EXPECT_EQ(audio_thread_add_events_callback_fd, socket_ret);
  EXPECT_EQ((struct cras_hfp*)audio_thread_add_events_callback_data, hfp);

  cras_floss_hfp_stop(hfp, CRAS_STREAM_OUTPUT);
  // Expect no stop sco call before CRAS_STREAM_INPUT is also stopped.
  EXPECT_EQ(floss_media_hfp_stop_sco_called, 0);
  EXPECT_EQ(cras_floss_hfp_get_fd(hfp), socket_ret);

  cras_floss_hfp_stop(hfp, CRAS_STREAM_INPUT);
  EXPECT_EQ(floss_media_hfp_stop_sco_called, 1);
  EXPECT_EQ(cras_floss_hfp_get_fd(hfp), -1);

  cras_floss_hfp_destroy(hfp);
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

// From cras_fl_pcm_iodev
struct cras_iodev* hfp_pcm_iodev_create(struct cras_hfp* hfp,
                                        enum CRAS_STREAM_DIRECTION dir) {
  hfp_pcm_iodev_create_hfp_val = hfp;
  hfp_pcm_iodev_create_called++;
  return hfp_pcm_iodev_create_ret;
}

void hfp_pcm_iodev_destroy(struct cras_iodev* iodev) {
  hfp_pcm_iodev_destroy_called++;
  return;
}

// From cras_hfp_alsa_io
struct cras_iodev* hfp_alsa_iodev_create(struct cras_iodev* aio,
                                         struct cras_bt_device* device,
                                         struct hfp_slc_handle* slc,
                                         struct cras_sco* sco,
                                         struct cras_hfp* hfp) {
  hfp_alsa_iodev_create_hfp_val = hfp;
  hfp_alsa_iodev_create_called++;
  return hfp_alsa_iodev_create_ret;
}

void hfp_alsa_iodev_destroy(struct cras_iodev* iodev) {
  hfp_alsa_iodev_destroy_called++;
  return;
}

// From cras_fl_media
int floss_media_hfp_start_sco_call(struct fl_media* fm,
                                   const char* addr,
                                   bool force_cvsd) {
  floss_media_hfp_start_sco_called++;
  return 0;
}

int floss_media_hfp_stop_sco_call(struct fl_media* fm, const char* addr) {
  floss_media_hfp_stop_sco_called++;
  return 0;
}

struct cras_iodev* cras_iodev_list_get_sco_pcm_iodev(
    enum CRAS_STREAM_DIRECTION direction) {
  return cras_iodev_list_get_sco_pcm_iodev_ret;
}

// From cras_system_state
bool cras_system_get_bt_hfp_offload_finch_applied() {
  return true;
}

bool cras_system_get_bt_wbs_enabled() {
  return true;
}

}  // extern "C"
