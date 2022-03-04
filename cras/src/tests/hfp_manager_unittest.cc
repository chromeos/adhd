// Copyright 2022 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <gtest/gtest.h>
#include "cras_types.h"

extern "C" {
#include "cras_hfp_manager.h"
#include "cras_iodev.h"
}

static struct cras_hfp* hfp_pcm_iodev_create_hfp_val;
static size_t hfp_pcm_iodev_create_called;
static size_t hfp_pcm_iodev_destroy_called;
static size_t floss_media_hfp_start_sco_called;
static size_t floss_media_hfp_stop_sco_called;
static thread_callback hfp_callback;
static void* hfp_callback_data;
static int audio_thread_config_events_callback_called;
static enum AUDIO_THREAD_EVENTS_CB_TRIGGER
    audio_thread_config_events_callback_trigger;
static const int fake_skt = 456;

void ResetStubData() {
  hfp_pcm_iodev_create_hfp_val = NULL;
  hfp_pcm_iodev_create_called = 0;
  hfp_pcm_iodev_destroy_called = 0;
  floss_media_hfp_start_sco_called = 0;
  floss_media_hfp_stop_sco_called = 0;
  hfp_callback = NULL;
  audio_thread_config_events_callback_called = 0;
}

namespace {

class HfpManagerTestSuite : public testing::Test {
 protected:
  virtual void SetUp() { ResetStubData(); }

  virtual void TearDown() {}
};

TEST_F(HfpManagerTestSuite, CreateDestroy) {
  struct cras_hfp* hfp = cras_floss_hfp_create(NULL, "addr");
  ASSERT_NE(hfp, (struct cras_hfp*)NULL);
  EXPECT_EQ(hfp, hfp_pcm_iodev_create_hfp_val);
  EXPECT_EQ(hfp_pcm_iodev_create_called, 2);

  // Expect another call to hfp create returns null.
  struct cras_hfp* expect_null = cras_floss_hfp_create(NULL, "addr2");
  EXPECT_EQ((void*)NULL, expect_null);

  cras_floss_hfp_destroy(hfp);
  EXPECT_EQ(hfp_pcm_iodev_destroy_called, 2);
}

TEST_F(HfpManagerTestSuite, StartStop) {
  struct cras_hfp* hfp = cras_floss_hfp_create(NULL, "addr");
  ASSERT_NE(hfp, (struct cras_hfp*)NULL);

  EXPECT_EQ(cras_floss_hfp_get_fd(hfp), -1);

  cras_floss_hfp_start(hfp, NULL, CRAS_STREAM_OUTPUT);
  EXPECT_EQ(floss_media_hfp_start_sco_called, 1);
  EXPECT_EQ(cras_floss_hfp_get_fd(hfp), fake_skt);

  cras_floss_hfp_start(hfp, NULL, CRAS_STREAM_INPUT);
  EXPECT_EQ(floss_media_hfp_start_sco_called, 1);

  cras_floss_hfp_stop(hfp, CRAS_STREAM_OUTPUT);
  // Expect no stop sco call before CRAS_STREAM_INPUT is also stopped.
  EXPECT_EQ(floss_media_hfp_stop_sco_called, 0);
  EXPECT_EQ(cras_floss_hfp_get_fd(hfp), fake_skt);

  cras_floss_hfp_stop(hfp, CRAS_STREAM_INPUT);
  EXPECT_EQ(floss_media_hfp_stop_sco_called, 1);
  EXPECT_EQ(cras_floss_hfp_get_fd(hfp), -1);
}

}  // namespace

extern "C" {

/* socket and connect */
int socket(int domain, int type, int protocol) {
  return fake_skt;
}

int connect(int sockfd, const struct sockaddr* addr, socklen_t addrlen) {
  return 0;
}

/* From audio_thread */
struct audio_thread_event_log* atlog;

void audio_thread_add_events_callback(int fd,
                                      thread_callback cb,
                                      void* data,
                                      int events) {
  hfp_callback = cb;
  hfp_callback_data = data;
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

/* From cras iodev list */

struct audio_thread* cras_iodev_list_get_audio_thread() {
  return NULL;
}

/* From cras_fl_pcm_iodev */
struct cras_iodev* hfp_pcm_iodev_create(struct cras_hfp* hfp,
                                        enum CRAS_STREAM_DIRECTION dir) {
  hfp_pcm_iodev_create_hfp_val = hfp;
  hfp_pcm_iodev_create_called++;
  return reinterpret_cast<struct cras_iodev*>(0x123);
}

void hfp_pcm_iodev_destroy(struct cras_iodev* iodev) {
  hfp_pcm_iodev_destroy_called++;
  return;
}

/* From cras_fl_media */
int floss_media_hfp_start_sco_call(struct fl_media* fm, const char* addr) {
  floss_media_hfp_start_sco_called++;
  return 0;
}

int floss_media_hfp_stop_sco_call(struct fl_media* fm, const char* addr) {
  floss_media_hfp_stop_sco_called++;
  return 0;
}

}  // extern "C"

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
