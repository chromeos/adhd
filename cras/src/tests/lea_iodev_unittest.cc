// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <gtest/gtest.h>
#include <stdint.h>
#include <stdio.h>

#include "cras/include/cras_types.h"
#include "cras/src/server/audio_thread.h"
#include "cras/src/server/audio_thread_log.h"
#include "cras/src/server/cras_audio_area.h"
#include "cras/src/server/cras_iodev.h"
#include "cras/src/server/cras_iodev_list.h"
#include "cras/src/server/cras_lea_manager.h"
#include "cras/src/tests/test_util.hh"
#include "third_party/utlist/utlist.h"

extern "C" {
// To test static functions.
#include "cras/src/server/cras_lea_iodev.c"
}

#define FAKE_SOCKET_FD 99;
static cras_audio_format format;

static unsigned cras_iodev_add_node_called;
static unsigned cras_iodev_rm_node_called;
static unsigned cras_iodev_set_active_node_called;
static unsigned cras_iodev_free_format_called;
static unsigned cras_iodev_free_resources_called;
static unsigned cras_iodev_list_add_called;
static unsigned cras_iodev_list_rm_called;
static cras_audio_area* mock_audio_area;
static unsigned cras_iodev_init_audio_area_called;
static unsigned cras_iodev_free_audio_area_called;
static unsigned cras_floss_lea_start_called;
static unsigned cras_floss_lea_stop_called;
static int cras_floss_lea_get_fd_ret;
static cras_iodev* cras_floss_lea_get_primary_idev_ret;
static cras_iodev* cras_floss_lea_get_primary_odev_ret;
static thread_callback write_callback;
static void* write_callback_data;
static int audio_thread_config_events_callback_called;
static enum AUDIO_THREAD_EVENTS_CB_TRIGGER
    audio_thread_config_events_callback_trigger;
static int cras_floss_lea_fill_format_called;
static int is_utf8_string_ret_value;
static int cras_iodev_list_suspend_dev_called;
static int cras_iodev_list_resume_dev_called;
static int cras_iodev_list_resume_dev_idx;
static int cras_floss_lea_is_idev_started_ret;
static int cras_floss_lea_is_odev_started_ret;
static int cras_floss_lea_set_active_called;
static int cras_floss_lea_configure_sink_for_voice_communication_called;
static int cras_floss_lea_configure_source_for_voice_communication_called;
static int cras_floss_lea_configure_source_for_media_called;

void ResetStubData() {
  cras_iodev_add_node_called = 0;
  cras_iodev_rm_node_called = 0;
  cras_iodev_set_active_node_called = 0;
  cras_iodev_free_format_called = 0;
  cras_iodev_free_resources_called = 0;
  cras_iodev_list_add_called = 0;
  cras_iodev_list_rm_called = 0;
  cras_iodev_init_audio_area_called = 0;
  cras_iodev_free_audio_area_called = 0;
  cras_floss_lea_start_called = 0;
  cras_floss_lea_stop_called = 0;
  cras_floss_lea_get_fd_ret = FAKE_SOCKET_FD;
  cras_floss_lea_get_primary_idev_ret = NULL;
  cras_floss_lea_get_primary_odev_ret = NULL;
  write_callback = NULL;
  audio_thread_config_events_callback_called = 0;
  audio_thread_config_events_callback_trigger = TRIGGER_NONE;
  cras_floss_lea_fill_format_called = 0;
  is_utf8_string_ret_value = 1;
  cras_iodev_list_suspend_dev_called = 0;
  cras_iodev_list_resume_dev_called = 0;
  cras_floss_lea_is_idev_started_ret = 0;
  cras_floss_lea_is_odev_started_ret = 0;
  cras_floss_lea_set_active_called = 0;
  cras_floss_lea_configure_sink_for_voice_communication_called = 0;
  cras_floss_lea_configure_source_for_voice_communication_called = 0;
  cras_floss_lea_configure_source_for_media_called = 0;
}

int iodev_set_lea_format(struct cras_iodev* iodev,
                         struct cras_audio_format* fmt) {
  fmt->format = SND_PCM_FORMAT_S16_LE;
  fmt->num_channels = 1;
  fmt->frame_rate = 32000;
  iodev->format = fmt;
  return 0;
}

unsigned int iodev_get_buffer(struct cras_iodev* iodev, unsigned int frame) {
  unsigned int frame_ret = frame;
  struct cras_audio_area* area;
  EXPECT_EQ(0, iodev->get_buffer(iodev, &area, &frame_ret));
  return frame_ret;
}

namespace {
class PcmIodev : public testing::Test {
 protected:
  virtual void SetUp() {
    ResetStubData();
    mock_audio_area = (cras_audio_area*)calloc(
        1, sizeof(*mock_audio_area) + sizeof(cras_channel_area) * 2);
    atlog = (audio_thread_event_log*)calloc(1, sizeof(audio_thread_event_log));
  }

  virtual void TearDown() {
    free(mock_audio_area);
    free(atlog);
  }
};

TEST_F(PcmIodev, CreateDestroyLeaIodev) {
  struct cras_iodev* idev;
  struct cras_iodev* odev;

  odev = lea_iodev_create(NULL, "name", 1, CRAS_STREAM_OUTPUT);
  EXPECT_NE(odev, (void*)NULL);
  EXPECT_EQ(odev->direction, CRAS_STREAM_OUTPUT);
  EXPECT_EQ(CRAS_BT_FLAG_FLOSS,
            CRAS_BT_FLAG_FLOSS & odev->active_node->btflags);
  EXPECT_EQ(CRAS_BT_FLAG_LEA, CRAS_BT_FLAG_LEA & odev->active_node->btflags);
  EXPECT_EQ(1, cras_iodev_add_node_called);
  EXPECT_EQ(1, cras_iodev_set_active_node_called);

  idev = lea_iodev_create(NULL, "name", 1, CRAS_STREAM_INPUT);
  EXPECT_NE(idev, (void*)NULL);
  EXPECT_EQ(idev->direction, CRAS_STREAM_INPUT);
  EXPECT_EQ(CRAS_BT_FLAG_FLOSS,
            CRAS_BT_FLAG_FLOSS & idev->active_node->btflags);
  EXPECT_EQ(CRAS_BT_FLAG_LEA, CRAS_BT_FLAG_LEA & idev->active_node->btflags);
  EXPECT_EQ(2, cras_iodev_add_node_called);
  EXPECT_EQ(2, cras_iodev_set_active_node_called);

  lea_iodev_destroy(odev);
  EXPECT_EQ(1, cras_iodev_rm_node_called);
  EXPECT_EQ(1, cras_iodev_list_rm_called);
  EXPECT_EQ(1, cras_iodev_free_resources_called);

  lea_iodev_destroy(idev);
  EXPECT_EQ(2, cras_iodev_rm_node_called);
  EXPECT_EQ(2, cras_iodev_list_rm_called);
  EXPECT_EQ(2, cras_iodev_free_resources_called);
}

TEST_F(PcmIodev, OpenLeaIdevThenOdev) {
  struct cras_iodev* idev;
  struct cras_iodev* odev;

  odev = lea_iodev_create(NULL, "name", 1, CRAS_STREAM_OUTPUT);
  EXPECT_NE(odev, (void*)NULL);
  EXPECT_EQ(odev->direction, CRAS_STREAM_OUTPUT);
  EXPECT_EQ(CRAS_BT_FLAG_FLOSS,
            CRAS_BT_FLAG_FLOSS & odev->active_node->btflags);
  EXPECT_EQ(CRAS_BT_FLAG_LEA, CRAS_BT_FLAG_LEA & odev->active_node->btflags);
  EXPECT_EQ(1, cras_iodev_add_node_called);
  EXPECT_EQ(1, cras_iodev_set_active_node_called);

  idev = lea_iodev_create(NULL, "name", 1, CRAS_STREAM_INPUT);
  EXPECT_NE(idev, (void*)NULL);
  EXPECT_EQ(idev->direction, CRAS_STREAM_INPUT);
  EXPECT_EQ(CRAS_BT_FLAG_FLOSS,
            CRAS_BT_FLAG_FLOSS & idev->active_node->btflags);
  EXPECT_EQ(CRAS_BT_FLAG_LEA, CRAS_BT_FLAG_LEA & idev->active_node->btflags);
  EXPECT_EQ(2, cras_iodev_add_node_called);
  EXPECT_EQ(2, cras_iodev_set_active_node_called);

  cras_floss_lea_get_primary_odev_ret = odev;
  cras_floss_lea_get_primary_idev_ret = idev;

  {
    CLEAR_AND_EVENTUALLY(
        EXPECT_EQ, cras_floss_lea_configure_sink_for_voice_communication_called,
        1);
    CLEAR_AND_EVENTUALLY(EXPECT_EQ, cras_iodev_list_suspend_dev_called, 0);
    CLEAR_AND_EVENTUALLY(EXPECT_EQ, cras_floss_lea_start_called, 1);
    idev->open_dev(idev);
    cras_floss_lea_is_idev_started_ret = 1;
  }

  {
    CLEAR_AND_EVENTUALLY(
        EXPECT_EQ,
        cras_floss_lea_configure_source_for_voice_communication_called, 1);
    CLEAR_AND_EVENTUALLY(EXPECT_EQ, cras_iodev_list_suspend_dev_called, 0);
    CLEAR_AND_EVENTUALLY(EXPECT_EQ, cras_floss_lea_start_called, 1);
    odev->open_dev(odev);
    cras_floss_lea_is_odev_started_ret = 1;
  }

  {
    CLEAR_AND_EVENTUALLY(EXPECT_EQ, cras_iodev_rm_node_called, 1);
    CLEAR_AND_EVENTUALLY(EXPECT_EQ, cras_iodev_list_rm_called, 1);
    CLEAR_AND_EVENTUALLY(EXPECT_EQ, cras_iodev_free_resources_called, 1);
    lea_iodev_destroy(odev);
  }

  {
    CLEAR_AND_EVENTUALLY(EXPECT_EQ, cras_iodev_rm_node_called, 1);
    CLEAR_AND_EVENTUALLY(EXPECT_EQ, cras_iodev_list_rm_called, 1);
    CLEAR_AND_EVENTUALLY(EXPECT_EQ, cras_iodev_free_resources_called, 1);
    lea_iodev_destroy(idev);
  }
}

TEST_F(PcmIodev, OpenLeaOdevThenIdev) {
  struct cras_iodev* idev;
  struct cras_iodev* odev;

  odev = lea_iodev_create(NULL, "name", 1, CRAS_STREAM_OUTPUT);
  EXPECT_NE(odev, (void*)NULL);
  EXPECT_EQ(odev->direction, CRAS_STREAM_OUTPUT);
  EXPECT_EQ(CRAS_BT_FLAG_FLOSS,
            CRAS_BT_FLAG_FLOSS & odev->active_node->btflags);
  EXPECT_EQ(CRAS_BT_FLAG_LEA, CRAS_BT_FLAG_LEA & odev->active_node->btflags);
  EXPECT_EQ(1, cras_iodev_add_node_called);
  EXPECT_EQ(1, cras_iodev_set_active_node_called);

  idev = lea_iodev_create(NULL, "name", 1, CRAS_STREAM_INPUT);
  EXPECT_NE(idev, (void*)NULL);
  EXPECT_EQ(idev->direction, CRAS_STREAM_INPUT);
  EXPECT_EQ(CRAS_BT_FLAG_FLOSS,
            CRAS_BT_FLAG_FLOSS & idev->active_node->btflags);
  EXPECT_EQ(CRAS_BT_FLAG_LEA, CRAS_BT_FLAG_LEA & idev->active_node->btflags);
  EXPECT_EQ(2, cras_iodev_add_node_called);
  EXPECT_EQ(2, cras_iodev_set_active_node_called);

  cras_floss_lea_get_primary_odev_ret = odev;
  cras_floss_lea_get_primary_idev_ret = idev;

  {
    CLEAR_AND_EVENTUALLY(EXPECT_EQ,
                         cras_floss_lea_configure_source_for_media_called, 1);
    CLEAR_AND_EVENTUALLY(EXPECT_EQ, cras_iodev_list_suspend_dev_called, 0);
    CLEAR_AND_EVENTUALLY(EXPECT_EQ, cras_floss_lea_start_called, 1);
    odev->open_dev(odev);
    cras_floss_lea_is_odev_started_ret = 1;
  }

  {
    CLEAR_AND_EVENTUALLY(
        EXPECT_EQ, cras_floss_lea_configure_sink_for_voice_communication_called,
        1);
    CLEAR_AND_EVENTUALLY(EXPECT_EQ, cras_iodev_list_suspend_dev_called, 1);
    CLEAR_AND_EVENTUALLY(EXPECT_EQ, cras_iodev_list_resume_dev_called, 1);
    CLEAR_AND_EVENTUALLY(EXPECT_EQ, cras_floss_lea_start_called, 1);
    idev->open_dev(idev);
    cras_floss_lea_is_idev_started_ret = 1;
  }

  {
    CLEAR_AND_EVENTUALLY(EXPECT_EQ, cras_iodev_rm_node_called, 1);
    CLEAR_AND_EVENTUALLY(EXPECT_EQ, cras_iodev_list_rm_called, 1);
    CLEAR_AND_EVENTUALLY(EXPECT_EQ, cras_iodev_free_resources_called, 1);
    lea_iodev_destroy(odev);
  }

  {
    CLEAR_AND_EVENTUALLY(EXPECT_EQ, cras_iodev_rm_node_called, 1);
    CLEAR_AND_EVENTUALLY(EXPECT_EQ, cras_iodev_list_rm_called, 1);
    CLEAR_AND_EVENTUALLY(EXPECT_EQ, cras_iodev_free_resources_called, 1);
    lea_iodev_destroy(idev);
  }
}

TEST_F(PcmIodev, CloseLeaIdevThenOdev) {
  struct cras_iodev* idev;
  struct cras_iodev* odev;

  odev = lea_iodev_create(NULL, "name", 1, CRAS_STREAM_OUTPUT);
  idev = lea_iodev_create(NULL, "name", 1, CRAS_STREAM_INPUT);

  cras_floss_lea_get_primary_odev_ret = odev;
  cras_floss_lea_get_primary_idev_ret = idev;

  idev->open_dev(idev);
  cras_floss_lea_is_idev_started_ret = 1;

  odev->open_dev(odev);
  cras_floss_lea_is_odev_started_ret = 1;

  {
    CLEAR_AND_EVENTUALLY(EXPECT_EQ,
                         cras_floss_lea_configure_source_for_media_called, 1);
    CLEAR_AND_EVENTUALLY(EXPECT_EQ, cras_iodev_list_suspend_dev_called, 1);
    CLEAR_AND_EVENTUALLY(EXPECT_EQ, cras_iodev_list_resume_dev_called, 1);
    CLEAR_AND_EVENTUALLY(EXPECT_EQ, cras_floss_lea_stop_called, 1);
    idev->close_dev(idev);
    cras_floss_lea_is_idev_started_ret = 0;
  }

  {
    CLEAR_AND_EVENTUALLY(EXPECT_EQ, cras_iodev_list_suspend_dev_called, 0);
    CLEAR_AND_EVENTUALLY(EXPECT_EQ, cras_floss_lea_stop_called, 1);
    odev->close_dev(odev);
    cras_floss_lea_is_odev_started_ret = 0;
  }

  lea_iodev_destroy(odev);
  lea_iodev_destroy(idev);
}

TEST_F(PcmIodev, CloseLeaOdevThenIdev) {
  struct cras_iodev* idev;
  struct cras_iodev* odev;

  odev = lea_iodev_create(NULL, "name", 1, CRAS_STREAM_OUTPUT);
  idev = lea_iodev_create(NULL, "name", 1, CRAS_STREAM_INPUT);

  cras_floss_lea_get_primary_odev_ret = odev;
  cras_floss_lea_get_primary_idev_ret = idev;

  idev->open_dev(idev);
  cras_floss_lea_is_idev_started_ret = 1;

  odev->open_dev(odev);
  cras_floss_lea_is_odev_started_ret = 1;

  {
    CLEAR_AND_EVENTUALLY(EXPECT_EQ, cras_iodev_list_suspend_dev_called, 0);
    CLEAR_AND_EVENTUALLY(EXPECT_EQ, cras_floss_lea_stop_called, 1);
    odev->close_dev(odev);
    cras_floss_lea_is_odev_started_ret = 0;
  }

  {
    CLEAR_AND_EVENTUALLY(EXPECT_EQ, cras_iodev_list_suspend_dev_called, 0);
    CLEAR_AND_EVENTUALLY(EXPECT_EQ, cras_floss_lea_stop_called, 1);
    idev->close_dev(idev);
    cras_floss_lea_is_idev_started_ret = 0;
  }

  lea_iodev_destroy(odev);
  lea_iodev_destroy(idev);
}

TEST_F(PcmIodev, TestLeaReadNotStarted) {
  int sock[2];
  struct cras_iodev* idev;
  uint8_t sample[200];
  struct timespec tstamp;

  ASSERT_EQ(0, socketpair(AF_UNIX, SOCK_STREAM, 0, sock));
  cras_floss_lea_get_fd_ret = sock[1];

  idev = lea_iodev_create(NULL, "name", 1, CRAS_STREAM_INPUT);
  // Mock the pcm fd and send some fake data
  send(sock[0], sample, 48, 0);
  lea_read((lea_io*)idev);

  // Ignore the data if !idev->started
  EXPECT_EQ(0, iodev_get_buffer(idev, 100));
  EXPECT_EQ(0, frames_queued(idev, &tstamp));

  lea_iodev_destroy(idev);
}

TEST_F(PcmIodev, TestLeaReadStarted) {
  int sock[2];
  struct cras_iodev* idev;
  struct lea_io* lea_idev;
  uint8_t sample[FLOSS_LEA_MAX_BUF_SIZE_BYTES] = {1};
  size_t format_bytes;
  struct timespec tstamp;

  ASSERT_EQ(0, socketpair(AF_UNIX, SOCK_STREAM, 0, sock));
  cras_floss_lea_get_fd_ret = sock[1];
  idev = lea_iodev_create(NULL, "name", 1, CRAS_STREAM_INPUT);
  lea_idev = (lea_io*)idev;
  iodev_set_lea_format(idev, &format);
  format_bytes = cras_get_format_bytes(idev->format);
  idev->configure_dev(idev);

  // Simple read
  send(sock[0], sample, 20 * format_bytes, 0);
  lea_read(lea_idev);
  // Try to request number of frames larger than availalea ones
  EXPECT_EQ(20, iodev_get_buffer(idev, 100));
  EXPECT_EQ(20, frames_queued(idev, &tstamp));

  EXPECT_EQ(0, idev->put_buffer(idev, 20));
  EXPECT_EQ(0, frames_queued(idev, &tstamp));

  // TODO(b/226386060): Add tests to cover the partial read case, such that
  // calling recv on sock[1] two consecutive times both return less than
  // required amount of data.
  lea_iodev_destroy(idev);
}

TEST_F(PcmIodev, TestLeaWriteNotStarted) {
  int rc;
  int sock[2];
  struct cras_iodev* odev;
  struct lea_io* lea_odev;
  uint8_t buf[200];

  ASSERT_EQ(0, socketpair(AF_UNIX, SOCK_STREAM, 0, sock));
  cras_floss_lea_get_fd_ret = sock[1];
  odev = lea_iodev_create(NULL, "name", 0, CRAS_STREAM_OUTPUT);
  lea_odev = (lea_io*)odev;

  lea_write((lea_io*)odev, 100);
  // Should still receive 100 bytes of data when odev is not started.
  rc = recv(sock[0], buf, sizeof(buf), 0);
  EXPECT_EQ(100, rc);
  EXPECT_EQ(0, buf_readable(lea_odev->pcm_buf));

  // Get 0 frames if not configured and started
  EXPECT_EQ(0, iodev_get_buffer(odev, 50));

  lea_iodev_destroy(odev);
}

TEST_F(PcmIodev, TestLeaCb) {
  int rc;
  int sock[2];
  uint8_t sample[200], buf[200];
  struct cras_iodev* odev;
  struct lea_io *lea_odev, *lea_idev;

  ASSERT_EQ(0, socketpair(AF_UNIX, SOCK_STREAM, 0, sock));
  cras_floss_lea_get_fd_ret = sock[1];
  cras_floss_lea_get_primary_odev_ret =
      lea_iodev_create(NULL, "name", 0, CRAS_STREAM_OUTPUT);
  cras_floss_lea_get_primary_idev_ret =
      lea_iodev_create(NULL, "name", 0, CRAS_STREAM_INPUT);

  odev = cras_floss_lea_get_primary_odev_ret;
  iodev_set_lea_format(odev, &format);
  odev->configure_dev(odev);

  lea_odev = (lea_io*)odev;
  lea_idev = (lea_io*)cras_floss_lea_get_primary_idev_ret;

  lea_odev->started = lea_idev->started = 1;

  EXPECT_EQ(-EPIPE, lea_socket_read_write_cb((void*)NULL, POLLERR));

  send(sock[0], sample, 100, 0);
  buf_increment_write(lea_odev->pcm_buf, 150);
  rc = lea_socket_read_write_cb((void*)NULL, POLLIN | POLLOUT);
  EXPECT_EQ(0, rc);

  EXPECT_EQ(100, buf_readable(lea_idev->pcm_buf));
  EXPECT_EQ(0, buf_readable(lea_odev->pcm_buf));
  rc = recv(sock[0], buf, 200, 0);
  EXPECT_EQ(150, rc);

  // After POLLHUP the cb should be removed.
  EXPECT_EQ(-EPIPE, lea_socket_read_write_cb((void*)NULL, POLLHUP));
  EXPECT_EQ(NULL, write_callback);
  EXPECT_EQ(NULL, write_callback_data);

  lea_iodev_destroy(cras_floss_lea_get_primary_odev_ret);
  lea_iodev_destroy(cras_floss_lea_get_primary_idev_ret);
}
}  // namespace

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

void cras_iodev_init_audio_area(struct cras_iodev* iodev) {
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

int cras_iodev_fill_odev_zeros(struct cras_iodev* odev,
                               unsigned int frames,
                               bool processing) {
  return (int)frames;
}

// Cras iodev list
int cras_iodev_list_add(struct cras_iodev* iodev) {
  cras_iodev_list_add_called++;
  return 0;
}

int cras_iodev_list_rm(struct cras_iodev* iodev) {
  cras_iodev_list_rm_called++;
  return 0;
}

struct audio_thread* cras_iodev_list_get_audio_thread() {
  return NULL;
}

int is_utf8_string(const char* string) {
  return is_utf8_string_ret_value;
}

void cras_iodev_list_suspend_dev(unsigned int dev_idx) {
  cras_iodev_list_suspend_dev_called++;
}

void cras_iodev_list_resume_dev(unsigned int dev_idx) {
  cras_iodev_list_resume_dev_called++;
  cras_iodev_list_resume_dev_idx = dev_idx;
}

// From ewma_power
void ewma_power_disable(struct ewma_power* ewma) {}

// From audio_thread
struct audio_thread_event_log* atlog;

// From cras_bt_log
struct cras_bt_event_log* btlog;

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

int audio_thread_rm_callback_sync(struct audio_thread* thread, int fd) {
  return 0;
}

void audio_thread_rm_callback(int fd) {
  write_callback = NULL;
  write_callback_data = NULL;
}

// LEA manager
int cras_floss_lea_start(struct cras_lea* lea,
                         thread_callback cb,
                         enum CRAS_STREAM_DIRECTION dir) {
  cras_floss_lea_start_called++;
  return 0;
}

int cras_floss_lea_stop(struct cras_lea* lea, enum CRAS_STREAM_DIRECTION dir) {
  cras_floss_lea_stop_called++;
  return 0;
}

void cras_floss_lea_set_active(struct cras_lea* lea,
                               int group_id,
                               unsigned enabled) {
  cras_floss_lea_set_active_called++;
  return;
}

int cras_floss_lea_get_fd(struct cras_lea* lea) {
  return cras_floss_lea_get_fd_ret;
}

struct cras_iodev* cras_floss_lea_get_primary_idev(struct cras_lea* lea) {
  return cras_floss_lea_get_primary_idev_ret;
}

struct cras_iodev* cras_floss_lea_get_primary_odev(struct cras_lea* lea) {
  return cras_floss_lea_get_primary_odev_ret;
}

int cras_floss_lea_fill_format(struct cras_lea* lea,
                               size_t** rates,
                               snd_pcm_format_t** formats,
                               size_t** channel_counts) {
  cras_floss_lea_fill_format_called++;

  free(*rates);
  free(*formats);
  free(*channel_counts);

  *rates = (size_t*)malloc(2 * sizeof(size_t));
  *formats = (snd_pcm_format_t*)malloc(2 * sizeof(snd_pcm_format_t));
  *channel_counts = (size_t*)malloc(2 * sizeof(size_t));

  return 0;
}

void cras_floss_lea_set_volume(struct cras_lea* lea, unsigned int volume) {
  return;
}

bool cras_floss_lea_is_idev_started(struct cras_lea* lea) {
  return cras_floss_lea_is_idev_started_ret;
}

bool cras_floss_lea_is_odev_started(struct cras_lea* lea) {
  return cras_floss_lea_is_odev_started_ret;
}

int cras_floss_lea_configure_sink_for_voice_communication(
    struct cras_lea* lea) {
  cras_floss_lea_configure_sink_for_voice_communication_called++;
  return 0;
}

int cras_floss_lea_configure_source_for_voice_communication(
    struct cras_lea* lea) {
  cras_floss_lea_configure_source_for_voice_communication_called++;
  return 0;
}

int cras_floss_lea_configure_source_for_media(struct cras_lea* lea) {
  cras_floss_lea_configure_source_for_media_called++;
  return 0;
}
}
