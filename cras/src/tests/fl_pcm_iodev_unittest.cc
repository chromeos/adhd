// Copyright 2021 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <gtest/gtest.h>
#include <stdint.h>
#include <stdio.h>

extern "C" {
// To test static functions.
#include "cras/src/server/audio_thread.h"
#include "cras/src/server/audio_thread_log.h"
#include "cras/src/server/cras_audio_area.h"
#include "cras/src/server/cras_bt_log.h"
#include "cras/src/server/cras_fl_pcm_iodev.c"
#include "cras/src/server/cras_iodev.h"
#include "cras/src/server/cras_iodev_list.h"
#include "third_party/utlist/utlist.h"
}

#define FAKE_SOCKET_FD 99;

static cras_audio_format format;

static unsigned cras_iodev_add_node_called;
static unsigned cras_iodev_rm_node_called;
static unsigned cras_iodev_set_active_node_called;
static unsigned cras_iodev_free_format_called;
static unsigned cras_iodev_free_resources_called;
static unsigned cras_iodev_list_add_output_called;
static unsigned cras_iodev_list_add_input_called;
static unsigned cras_iodev_list_rm_output_called;
static unsigned cras_iodev_list_rm_input_called;
static cras_audio_area* mock_audio_area;
static unsigned cras_iodev_init_audio_area_called;
static unsigned cras_iodev_free_audio_area_called;
static unsigned cras_floss_a2dp_start_called;
static unsigned cras_floss_a2dp_stop_called;
static int cras_floss_a2dp_get_fd_ret;
static unsigned cras_floss_hfp_start_called;
static unsigned cras_floss_hfp_stop_called;
static int cras_floss_hfp_get_fd_ret;
static bool cras_floss_hfp_get_wbs_supported_ret;
static cras_iodev* cras_floss_hfp_get_input_iodev_ret;
static cras_iodev* cras_floss_hfp_get_output_iodev_ret;
static unsigned cras_floss_a2dp_cancel_suspend_called;
static unsigned cras_floss_a2dp_schedule_suspend_called;
static thread_callback write_callback;
static void* write_callback_data;
static int audio_thread_config_events_callback_called;
static enum AUDIO_THREAD_EVENTS_CB_TRIGGER
    audio_thread_config_events_callback_trigger;
static int cras_floss_a2dp_fill_format_called;
static int cras_floss_hfp_fill_format_called;

void ResetStubData() {
  cras_iodev_add_node_called = 0;
  cras_iodev_rm_node_called = 0;
  cras_iodev_set_active_node_called = 0;
  cras_iodev_free_format_called = 0;
  cras_iodev_free_resources_called = 0;
  cras_iodev_list_add_output_called = 0;
  cras_iodev_list_add_input_called = 0;
  cras_iodev_list_rm_output_called = 0;
  cras_iodev_list_rm_input_called = 0;
  cras_iodev_init_audio_area_called = 0;
  cras_iodev_free_audio_area_called = 0;
  cras_floss_a2dp_start_called = 0;
  cras_floss_a2dp_stop_called = 0;
  cras_floss_a2dp_get_fd_ret = FAKE_SOCKET_FD;
  cras_floss_hfp_start_called = 0;
  cras_floss_hfp_stop_called = 0;
  cras_floss_hfp_get_fd_ret = FAKE_SOCKET_FD;
  cras_floss_hfp_get_input_iodev_ret = NULL;
  cras_floss_hfp_get_output_iodev_ret = NULL;
  cras_floss_hfp_get_wbs_supported_ret = false;
  cras_floss_a2dp_cancel_suspend_called = 0;
  cras_floss_a2dp_schedule_suspend_called = 0;
  write_callback = NULL;
  audio_thread_config_events_callback_called = 0;
  audio_thread_config_events_callback_trigger = TRIGGER_NONE;
  cras_floss_a2dp_fill_format_called = 0;
  cras_floss_hfp_fill_format_called = 0;
  if (!mock_audio_area) {
    mock_audio_area = (cras_audio_area*)calloc(
        1, sizeof(*mock_audio_area) + sizeof(cras_channel_area) * 2);
  }
  btlog = cras_bt_event_log_init();
}

int iodev_set_format(struct cras_iodev* iodev, struct cras_audio_format* fmt) {
  fmt->format = SND_PCM_FORMAT_S16_LE;
  fmt->num_channels = 2;
  fmt->frame_rate = 48000;
  iodev->format = fmt;
  return 0;
}

int iodev_set_hfp_format(struct cras_iodev* iodev,
                         struct cras_audio_format* fmt) {
  fmt->format = SND_PCM_FORMAT_S16_LE;
  fmt->num_channels = 1;
  fmt->frame_rate = 8000;
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
    atlog = (audio_thread_event_log*)calloc(1, sizeof(audio_thread_event_log));
  }

  virtual void TearDown() {
    free(atlog);
    cras_bt_event_log_deinit(btlog);
  }
};

TEST_F(PcmIodev, CreateDestroyA2dpPcmIodev) {
  struct cras_iodev* iodev;

  iodev = a2dp_pcm_iodev_create(NULL, 0, 0, 0);

  EXPECT_NE(iodev, (void*)NULL);
  EXPECT_EQ(iodev->direction, CRAS_STREAM_OUTPUT);

  EXPECT_EQ(1, cras_iodev_add_node_called);
  EXPECT_EQ(1, cras_iodev_set_active_node_called);
  EXPECT_EQ(1, cras_floss_a2dp_fill_format_called);

  EXPECT_EQ(CRAS_BT_FLAG_FLOSS,
            CRAS_BT_FLAG_FLOSS & iodev->active_node->btflags);
  EXPECT_EQ(CRAS_BT_FLAG_A2DP, CRAS_BT_FLAG_A2DP & iodev->active_node->btflags);

  a2dp_pcm_iodev_destroy(iodev);

  EXPECT_EQ(1, cras_iodev_rm_node_called);
  EXPECT_EQ(1, cras_iodev_list_rm_output_called);
  EXPECT_EQ(1, cras_iodev_free_resources_called);
}

TEST_F(PcmIodev, OpenCloseA2dpPcmIodev) {
  struct cras_iodev* iodev;

  iodev = a2dp_pcm_iodev_create(NULL, 0, 0, 0);

  iodev_set_format(iodev, &format);
  iodev->configure_dev(iodev);
  iodev->state = CRAS_IODEV_STATE_NORMAL_RUN;

  EXPECT_EQ(1, cras_floss_a2dp_start_called);
  EXPECT_EQ(1, cras_iodev_init_audio_area_called);
  EXPECT_NE(write_callback, (void*)NULL);
  EXPECT_EQ(1, audio_thread_config_events_callback_called);
  EXPECT_EQ(TRIGGER_NONE, audio_thread_config_events_callback_trigger);

  iodev->close_dev(iodev);
  EXPECT_EQ(1, cras_floss_a2dp_stop_called);
  EXPECT_EQ(1, cras_floss_a2dp_cancel_suspend_called);
  EXPECT_EQ(1, cras_iodev_free_format_called);
  EXPECT_EQ(1, cras_iodev_free_audio_area_called);

  a2dp_pcm_iodev_destroy(iodev);
}

TEST_F(PcmIodev, CreateDestroyHfpPcmIodev) {
  struct cras_iodev *idev, *odev;

  odev = hfp_pcm_iodev_create(NULL, CRAS_STREAM_OUTPUT);

  EXPECT_NE(odev, (void*)NULL);
  EXPECT_EQ(odev->direction, CRAS_STREAM_OUTPUT);

  EXPECT_EQ(1, cras_floss_hfp_fill_format_called);
  EXPECT_EQ(1, cras_iodev_add_node_called);
  EXPECT_EQ(1, cras_iodev_set_active_node_called);

  EXPECT_EQ(CRAS_BT_FLAG_FLOSS,
            CRAS_BT_FLAG_FLOSS & odev->active_node->btflags);
  EXPECT_EQ(CRAS_BT_FLAG_HFP, CRAS_BT_FLAG_HFP & odev->active_node->btflags);

  idev = hfp_pcm_iodev_create(NULL, CRAS_STREAM_INPUT);

  EXPECT_NE(idev, (void*)NULL);
  EXPECT_EQ(idev->direction, CRAS_STREAM_INPUT);

  EXPECT_EQ(2, cras_floss_hfp_fill_format_called);
  EXPECT_EQ(2, cras_iodev_add_node_called);
  EXPECT_EQ(2, cras_iodev_set_active_node_called);

  EXPECT_EQ(CRAS_BT_FLAG_FLOSS,
            CRAS_BT_FLAG_FLOSS & idev->active_node->btflags);
  EXPECT_EQ(CRAS_BT_FLAG_HFP, CRAS_BT_FLAG_HFP & idev->active_node->btflags);

  hfp_pcm_iodev_destroy(odev);

  EXPECT_EQ(1, cras_iodev_rm_node_called);
  EXPECT_EQ(1, cras_iodev_list_rm_output_called);
  EXPECT_EQ(1, cras_iodev_free_resources_called);

  hfp_pcm_iodev_destroy(idev);

  EXPECT_EQ(2, cras_iodev_rm_node_called);
  EXPECT_EQ(1, cras_iodev_list_rm_input_called);
  EXPECT_EQ(2, cras_iodev_free_resources_called);
}

TEST_F(PcmIodev, TestHfpReadNotStarted) {
  int sock[2];
  struct cras_iodev* idev;
  uint8_t sample[200];
  struct timespec tstamp;

  ASSERT_EQ(0, socketpair(AF_UNIX, SOCK_STREAM, 0, sock));
  cras_floss_hfp_get_fd_ret = sock[1];

  idev = hfp_pcm_iodev_create(NULL, CRAS_STREAM_INPUT);
  // Mock the pcm fd and send some fake data
  send(sock[0], sample, 48, 0);
  hfp_read((fl_pcm_io*)idev);

  // Ignore the data if !idev->started
  EXPECT_EQ(0, iodev_get_buffer(idev, 100));
  EXPECT_EQ(0, frames_queued(idev, &tstamp));

  hfp_pcm_iodev_destroy(idev);
}

TEST_F(PcmIodev, TestHfpReadStarted) {
  int sock[2];
  struct cras_iodev* idev;
  struct fl_pcm_io* pcm_idev;
  uint8_t sample[FLOSS_HFP_MAX_BUF_SIZE_BYTES] = {1};
  unsigned int pcm_buf_length;
  size_t format_bytes;
  struct timespec tstamp;

  ASSERT_EQ(0, socketpair(AF_UNIX, SOCK_STREAM, 0, sock));
  cras_floss_hfp_get_fd_ret = sock[1];
  idev = hfp_pcm_iodev_create(NULL, CRAS_STREAM_INPUT);
  pcm_idev = (fl_pcm_io*)idev;
  iodev_set_hfp_format(idev, &format);
  format_bytes = cras_get_format_bytes(idev->format);
  idev->configure_dev(idev);
  pcm_buf_length = pcm_idev->pcm_buf->used_size;

  // Simple read
  send(sock[0], sample, 20 * format_bytes, 0);
  hfp_read(pcm_idev);
  // Try to request number of frames larger than available ones
  EXPECT_EQ(20, iodev_get_buffer(idev, 100));
  EXPECT_EQ(20, frames_queued(idev, &tstamp));

  EXPECT_EQ(0, idev->put_buffer(idev, 20));
  EXPECT_EQ(0, frames_queued(idev, &tstamp));

  /* Send (max - 10) frames of data. 20 + max - 10 > max so we can test the case
   * that data lives across the ring buffer boundary. */
  send(sock[0], sample, pcm_buf_length - 10 * format_bytes, 0);
  hfp_read(pcm_idev);

  // Check that the data is correctly write into the buffer and queued.
  EXPECT_EQ(pcm_buf_length / format_bytes - 10, frames_queued(idev, &tstamp));

  // Should be able to read all data from 20 to the end of the ring buffer
  EXPECT_EQ((pcm_buf_length - 20 * format_bytes) / format_bytes,
            iodev_get_buffer(idev, (pcm_buf_length / format_bytes)));
  EXPECT_EQ(0, idev->put_buffer(
                   idev, (pcm_buf_length - 20 * format_bytes) / format_bytes));

  // Check that the remaining 10 frames are there.
  EXPECT_EQ(10, iodev_get_buffer(idev, (pcm_buf_length / format_bytes)));

  /* TODO(b/226386060): Add tests to cover the partial read case, such that
   * calling recv on sock[1] two consecutive times both return less than
   * required amount of data. */
  hfp_pcm_iodev_destroy(idev);
}

TEST_F(PcmIodev, TestHfpWriteNotStarted) {
  int rc;
  int sock[2];
  struct cras_iodev* odev;
  struct fl_pcm_io* pcm_odev;
  uint8_t buf[200];

  ASSERT_EQ(0, socketpair(AF_UNIX, SOCK_STREAM, 0, sock));
  cras_floss_hfp_get_fd_ret = sock[1];
  odev = hfp_pcm_iodev_create(NULL, CRAS_STREAM_OUTPUT);
  pcm_odev = (fl_pcm_io*)odev;

  hfp_write((fl_pcm_io*)odev, 100);
  // Should still receive 100 bytes of data when odev is not started.
  rc = recv(sock[0], buf, sizeof(buf), 0);
  EXPECT_EQ(100, rc);
  EXPECT_EQ(0, buf_readable(pcm_odev->pcm_buf));

  // Get 0 frames if not configured and started
  EXPECT_EQ(0, iodev_get_buffer(odev, 50));

  hfp_pcm_iodev_destroy(odev);
}

TEST_F(PcmIodev, TestHfpWriteStarted) {
  int rc;
  int sock[2];
  struct cras_iodev* odev;
  struct fl_pcm_io* pcm_odev;
  uint8_t buf[FLOSS_HFP_MAX_BUF_SIZE_BYTES];
  unsigned int pcm_buf_length, available;
  size_t format_bytes;
  struct timespec tstamp;

  ASSERT_EQ(0, socketpair(AF_UNIX, SOCK_STREAM, 0, sock));
  cras_floss_hfp_get_fd_ret = sock[1];
  odev = hfp_pcm_iodev_create(NULL, CRAS_STREAM_OUTPUT);
  pcm_odev = (fl_pcm_io*)odev;
  pcm_buf_length = pcm_odev->pcm_buf->used_size;
  iodev_set_hfp_format(odev, &format);
  format_bytes = cras_get_format_bytes(odev->format);
  odev->configure_dev(odev);

  // Write offset: 150.
  available = iodev_get_buffer(odev, 150);
  EXPECT_EQ(150, available);
  EXPECT_EQ(0, odev->put_buffer(odev, available));

  hfp_write(pcm_odev, 100 * format_bytes);
  // Read at max target_len of data.
  rc = recv(sock[0], buf, pcm_buf_length, 0);
  EXPECT_EQ(100 * format_bytes, rc);
  EXPECT_EQ(50, frames_queued(odev, &tstamp));

  hfp_write(pcm_odev, 50 * format_bytes);
  // Read as much as data.
  rc = recv(sock[0], buf, pcm_buf_length, 0);
  EXPECT_EQ(50 * format_bytes, rc);
  EXPECT_EQ(0, frames_queued(odev, &tstamp));
  EXPECT_EQ(0, buf_readable(pcm_odev->pcm_buf));

  // Fill the buffer to its boundary.
  available = iodev_get_buffer(odev, pcm_buf_length / format_bytes);
  EXPECT_EQ(pcm_buf_length / format_bytes - 150, available);
  EXPECT_EQ(0, odev->put_buffer(odev, available));

  available = iodev_get_buffer(odev, pcm_buf_length / format_bytes);
  EXPECT_EQ(150, available);
  // Fill 50 more frames.
  EXPECT_EQ(0, odev->put_buffer(odev, 50));
  EXPECT_EQ(pcm_buf_length / format_bytes - 150 + 50,
            frames_queued(odev, &tstamp));

  // Write all data in the ring buffer out.
  hfp_write(pcm_odev, pcm_buf_length - 100 * format_bytes);
  // Read as much as data.
  rc = recv(sock[0], buf, pcm_buf_length, 0);

  // All data in the buffer should be sent and digested.
  EXPECT_EQ(pcm_buf_length - 100 * format_bytes, rc);
  EXPECT_EQ(0, frames_queued(odev, &tstamp));
  /* The write offset is at 50 and the buffer should retrieve the space for
   * next write. */
  EXPECT_EQ(pcm_buf_length / format_bytes - 50,
            iodev_get_buffer(odev, pcm_buf_length / format_bytes));

  /* TODO(b/226386060): Add tests to cover the partial write case, such that
   * calling send on sock[1] two consecutive times both write less than
   * required amount of data. */
  hfp_pcm_iodev_destroy(odev);
}

TEST_F(PcmIodev, TestHfpCb) {
  int rc;
  int sock[2];
  uint8_t sample[200], buf[200];
  struct cras_iodev* odev;
  struct fl_pcm_io *pcm_odev, *pcm_idev;

  ASSERT_EQ(0, socketpair(AF_UNIX, SOCK_STREAM, 0, sock));
  cras_floss_hfp_get_fd_ret = sock[1];
  cras_floss_hfp_get_output_iodev_ret =
      hfp_pcm_iodev_create(NULL, CRAS_STREAM_OUTPUT);
  cras_floss_hfp_get_input_iodev_ret =
      hfp_pcm_iodev_create(NULL, CRAS_STREAM_INPUT);

  odev = cras_floss_hfp_get_output_iodev_ret;
  iodev_set_hfp_format(odev, &format);
  odev->configure_dev(odev);

  pcm_odev = (fl_pcm_io*)odev;
  pcm_idev = (fl_pcm_io*)cras_floss_hfp_get_input_iodev_ret;

  pcm_odev->started = pcm_idev->started = 1;

  EXPECT_EQ(-EPIPE, hfp_socket_read_write_cb((void*)NULL, POLLERR));

  /* Output device should try to write the same number of bytes as input device
   * read. */
  send(sock[0], sample, 100, 0);
  buf_increment_write(pcm_odev->pcm_buf, 150);
  rc = hfp_socket_read_write_cb((void*)NULL, POLLIN);
  EXPECT_EQ(0, rc);

  EXPECT_EQ(100, buf_readable(pcm_idev->pcm_buf));
  EXPECT_EQ(50, buf_readable(pcm_odev->pcm_buf));
  rc = recv(sock[0], buf, 200, 0);
  EXPECT_EQ(100, rc);

  // After POLLHUP the cb should be removed.
  EXPECT_EQ(-EPIPE, hfp_socket_read_write_cb((void*)NULL, POLLHUP));
  EXPECT_EQ(NULL, write_callback);
  EXPECT_EQ(NULL, write_callback_data);
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

int cras_iodev_fill_odev_zeros(struct cras_iodev* odev,
                               unsigned int frames,
                               bool underrun) {
  return 0;
}

// Cras iodev list
int cras_iodev_list_add_output(struct cras_iodev* output) {
  cras_iodev_list_add_output_called++;
  return 0;
}

int cras_iodev_list_add_input(struct cras_iodev* input) {
  cras_iodev_list_add_input_called++;
  return 0;
}

int cras_iodev_list_rm_output(struct cras_iodev* output) {
  cras_iodev_list_rm_output_called++;
  return 0;
}

int cras_iodev_list_rm_input(struct cras_iodev* output) {
  cras_iodev_list_rm_input_called++;
  return 0;
}

struct audio_thread* cras_iodev_list_get_audio_thread() {
  return NULL;
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
                          struct cras_audio_format* fmt) {
  cras_floss_a2dp_start_called++;
  return 0;
}

int cras_floss_a2dp_stop(struct cras_a2dp* a2dp) {
  cras_floss_a2dp_stop_called++;
  return 0;
}

int cras_floss_a2dp_get_fd(struct cras_a2dp* a2dp) {
  return cras_floss_a2dp_get_fd_ret;
}

void cras_floss_a2dp_set_volume(struct cras_a2dp* a2dp, unsigned int volume) {
  return;
}

void cras_floss_a2dp_delay_sync(struct cras_a2dp* a2dp,
                                unsigned int init_msec,
                                unsigned int period_msec) {
  return;
}

void cras_floss_a2dp_set_active(struct cras_a2dp* a2dp, unsigned enabled) {
  return;
}

// HFP manager
int cras_floss_hfp_start(struct cras_hfp* hfp,
                         thread_callback cb,
                         enum CRAS_STREAM_DIRECTION dir) {
  cras_floss_hfp_start_called++;
  return 0;
}

int cras_floss_hfp_stop(struct cras_hfp* hfp, enum CRAS_STREAM_DIRECTION dir) {
  cras_floss_hfp_stop_called++;
  return 0;
}

int cras_floss_hfp_get_fd(struct cras_hfp* hfp) {
  return cras_floss_hfp_get_fd_ret;
}

struct cras_iodev* cras_floss_hfp_get_input_iodev(struct cras_hfp* hfp) {
  return cras_floss_hfp_get_input_iodev_ret;
}

struct cras_iodev* cras_floss_hfp_get_output_iodev(struct cras_hfp* hfp) {
  return cras_floss_hfp_get_output_iodev_ret;
}

const char* cras_floss_hfp_get_display_name(struct cras_hfp* hfp) {
  return "hfp";
}

const char* cras_floss_hfp_get_addr(struct cras_hfp* hfp) {
  return "11:22:33:44:55:66";
}

bool cras_floss_hfp_get_wbs_supported(struct cras_hfp* hfp) {
  return cras_floss_hfp_get_wbs_supported_ret;
}

int cras_floss_hfp_fill_format(struct cras_hfp* hfp,
                               size_t** rates,
                               snd_pcm_format_t** formats,
                               size_t** channel_counts) {
  cras_floss_hfp_fill_format_called++;
  *rates = (size_t*)malloc(sizeof(**rates));
  *formats = (snd_pcm_format_t*)malloc(sizeof(**formats));
  *channel_counts = (size_t*)malloc(sizeof(**channel_counts));
  return 0;
}

void cras_floss_hfp_set_volume(struct cras_hfp* hfp, unsigned int volume) {
  return;
}

int cras_audio_thread_event_a2dp_throttle() {
  return 0;
}

void cras_floss_a2dp_cancel_suspend(struct cras_a2dp* a2dp) {
  cras_floss_a2dp_cancel_suspend_called++;
}

void cras_floss_a2dp_schedule_suspend(struct cras_a2dp* a2dp,
                                      unsigned int msec,
                                      enum A2DP_EXIT_CODE) {
  cras_floss_a2dp_schedule_suspend_called++;
}

void cras_floss_a2dp_update_write_status(struct cras_a2dp* a2dp,
                                         bool write_success) {
  return;
}

int cras_audio_thread_event_a2dp_overrun() {
  return 0;
}
}
