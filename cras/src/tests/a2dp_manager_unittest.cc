// Copyright 2021 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <gtest/gtest.h>

#include "cras/src/tests/test_util.h"

extern "C" {
#include "cras/src/server/cras_a2dp_manager.h"
#include "cras/src/server/cras_bt_log.h"
#include "cras/src/server/cras_fl_media.h"
#include "cras/src/server/cras_iodev.h"
#include "cras/src/server/cras_main_message.h"
#include "cras_audio_format.h"
}
static struct cras_a2dp* a2dp_pcm_iodev_create_a2dp_val;
static struct cras_iodev* a2dp_pcm_iodev_create_ret;
static struct cras_iodev* a2dp_pcm_iodev_destroy_iodev_val;
static int a2dp_pcm_update_bt_stack_delay_called;
static cras_main_message* cras_main_message_send_msg;
static cras_message_callback cras_main_message_add_handler_callback;
static void* cras_main_message_add_handler_callback_data;
static int cras_tm_create_timer_called;
static int cras_tm_cancel_timer_called;
static void (*cras_tm_create_timer_cb)(struct cras_timer* t, void* data);
static void* cras_tm_create_timer_cb_data;
static struct cras_timer* cras_tm_cancel_timer_arg;
static struct cras_timer* cras_tm_create_timer_ret;
static const int fake_skt = 456;
static int floss_media_a2dp_set_active_device_called;
static int floss_media_a2dp_set_audio_config_called;
static int floss_media_a2dp_set_audio_config_rate;
static int floss_media_a2dp_set_audio_config_bps;
static int floss_media_a2dp_set_audio_config_channels;
static int floss_media_a2dp_start_audio_request_called;
static int floss_media_a2dp_stop_audio_request_called;
static int floss_media_a2dp_set_volume_called;
static int floss_media_a2dp_set_volume_arg;
static int floss_media_a2dp_get_presentation_position_called;
static int floss_media_a2dp_suspend_called;
static struct cras_fl_a2dp_codec_config a2dp_codecs;

void ResetStubData() {
  a2dp_pcm_update_bt_stack_delay_called = 0;
  floss_media_a2dp_set_active_device_called = 0;
  floss_media_a2dp_set_audio_config_called = 0;
  floss_media_a2dp_set_audio_config_rate = 0;
  floss_media_a2dp_set_audio_config_bps = 0;
  floss_media_a2dp_set_audio_config_channels = 0;
  floss_media_a2dp_start_audio_request_called = 0;
  floss_media_a2dp_stop_audio_request_called = 0;
  floss_media_a2dp_set_volume_called = 0;
  floss_media_a2dp_set_volume_arg = 0;
  floss_media_a2dp_get_presentation_position_called = 0;
  cras_tm_create_timer_called = 0;
  cras_tm_cancel_timer_called = 0;
  cras_tm_create_timer_ret = NULL;
  a2dp_codecs.codec_type = FL_A2DP_CODEC_SRC_SBC;
}

namespace {

class A2dpManagerTestSuite : public testing::Test {
 protected:
  virtual void SetUp() {
    ResetStubData();
    btlog = cras_bt_event_log_init();
  }

  virtual void TearDown() {
    if (cras_main_message_send_msg) {
      free(cras_main_message_send_msg);
    }
    if (a2dp_pcm_iodev_create_ret) {
      free(a2dp_pcm_iodev_create_ret);
      a2dp_pcm_iodev_create_ret = NULL;
    }
    cras_bt_event_log_deinit(btlog);
  }
};

TEST_F(A2dpManagerTestSuite, CreateFailed) {
  a2dp_pcm_iodev_create_ret = NULL;
  // Failing to create a2dp_pcm_iodev should fail the a2dp_create
  ASSERT_EQ(cras_floss_a2dp_create(NULL, "addr", "name", &a2dp_codecs),
            (struct cras_a2dp*)NULL);

  a2dp_pcm_iodev_create_ret =
      (struct cras_iodev*)calloc(1, sizeof(struct cras_iodev));

  // NULL a2dp_codec_configs should fail the a2dp_create without a crash
  ASSERT_EQ(cras_floss_a2dp_create(NULL, "addr", "name",
                                   (struct cras_fl_a2dp_codec_config*)NULL),
            (struct cras_a2dp*)NULL);

  // Unsupported codecs should fail the a2dp_create without a crash
  a2dp_codecs.codec_type = FL_A2DP_CODEC_SINK_AAC;
  ASSERT_EQ(cras_floss_a2dp_create(NULL, "addr", "name", &a2dp_codecs),
            (struct cras_a2dp*)NULL);
}

TEST_F(A2dpManagerTestSuite, CreateDestroy) {
  struct cras_a2dp* a2dp;

  a2dp_pcm_iodev_create_ret =
      (struct cras_iodev*)calloc(1, sizeof(struct cras_iodev));
  a2dp = cras_floss_a2dp_create(NULL, "addr", "name", &a2dp_codecs);
  ASSERT_NE(a2dp, (struct cras_a2dp*)NULL);
  EXPECT_EQ(a2dp, a2dp_pcm_iodev_create_a2dp_val);
  EXPECT_EQ(strncmp("name", cras_floss_a2dp_get_display_name(a2dp), 4), 0);

  cras_floss_a2dp_destroy(a2dp);
  EXPECT_EQ(a2dp_pcm_iodev_destroy_iodev_val, a2dp_pcm_iodev_create_ret);
}

TEST_F(A2dpManagerTestSuite, StartStop) {
  a2dp_pcm_iodev_create_ret =
      (struct cras_iodev*)calloc(1, sizeof(struct cras_iodev));
  struct cras_audio_format fmt;
  struct cras_a2dp* a2dp =
      cras_floss_a2dp_create(NULL, "addr", "name", &a2dp_codecs);

  ASSERT_NE(a2dp, (struct cras_a2dp*)NULL);

  // Assert the format converts to the correct bitmap as Floss defined
  fmt.frame_rate = 44100;
  fmt.format = SND_PCM_FORMAT_S32_LE;
  fmt.num_channels = 2;
  EXPECT_EQ(0, cras_floss_a2dp_start(a2dp, &fmt));
  EXPECT_EQ(fake_skt, cras_floss_a2dp_get_fd(a2dp));
  EXPECT_EQ(floss_media_a2dp_set_active_device_called, 0);
  EXPECT_EQ(floss_media_a2dp_set_audio_config_called, 1);
  EXPECT_EQ(floss_media_a2dp_set_audio_config_rate, FL_RATE_44100);
  EXPECT_EQ(floss_media_a2dp_set_audio_config_bps, FL_SAMPLE_32);
  EXPECT_EQ(floss_media_a2dp_set_audio_config_channels, FL_MODE_STEREO);
  EXPECT_EQ(floss_media_a2dp_start_audio_request_called, 1);

  cras_floss_a2dp_stop(a2dp);
  EXPECT_EQ(floss_media_a2dp_stop_audio_request_called, 1);
  cras_floss_a2dp_destroy(a2dp);
}

TEST_F(A2dpManagerTestSuite, DelaySync) {
  struct cras_audio_format fmt;
  struct cras_a2dp* a2dp;

  a2dp_pcm_iodev_create_ret =
      (struct cras_iodev*)calloc(1, sizeof(struct cras_iodev));
  a2dp = cras_floss_a2dp_create(NULL, "addr", "name", &a2dp_codecs);
  ASSERT_NE(a2dp, (struct cras_a2dp*)NULL);

  // Assert the format converts to the correct bitmap as Floss defined
  fmt.frame_rate = 44100;
  fmt.format = SND_PCM_FORMAT_S32_LE;
  fmt.num_channels = 2;
  EXPECT_EQ(0, cras_floss_a2dp_start(a2dp, &fmt));
  EXPECT_EQ(fake_skt, cras_floss_a2dp_get_fd(a2dp));

  cras_tm_create_timer_ret = reinterpret_cast<struct cras_timer*>(0x123);
  cras_floss_a2dp_delay_sync(a2dp, 100, 1000);
  EXPECT_EQ(1, cras_tm_create_timer_called);

  cras_tm_create_timer_cb(NULL, cras_tm_create_timer_cb_data);
  EXPECT_EQ(1, floss_media_a2dp_get_presentation_position_called);
  EXPECT_EQ(2, cras_tm_create_timer_called);

  cras_floss_a2dp_stop(a2dp);
  EXPECT_EQ(floss_media_a2dp_stop_audio_request_called, 1);
  EXPECT_EQ(1, cras_tm_cancel_timer_called);
  cras_floss_a2dp_destroy(a2dp);
}

TEST(A2dpManager, FillFormat) {
  size_t *supported_channel_counts, *supported_rates;
  snd_pcm_format_t* supported_formats;
  int num_expected_rates = 1;
  size_t expected_rates[] = {44100};
  int num_unexpected_rates = 3;
  size_t unexpected_rates[] = {48000, 96000, 192000};
  int num_expected_formats = 1;
  snd_pcm_format_t expected_formats[] = {SND_PCM_FORMAT_S16_LE};
  int num_unexpected_formats = 2;
  snd_pcm_format_t unexpected_formats[] = {SND_PCM_FORMAT_S24_LE,
                                           SND_PCM_FORMAT_S32_LE};
  int num_expected_channel_counts = 1;
  size_t expected_channel_counts[] = {2};
  int num_unexpected_channel_counts = 1;
  size_t unexpected_channel_counts[] = {1};

  // Expect Floss defined bitmap converts to supported formats array.
  cras_floss_a2dp_fill_format(FL_RATE_44100 | FL_RATE_48000 | FL_RATE_16000,
                              FL_SAMPLE_16 | FL_SAMPLE_24,
                              FL_MODE_MONO | FL_MODE_STEREO, &supported_rates,
                              &supported_formats, &supported_channel_counts);
  for (int n = 0; n < num_expected_rates; n++) {
    int found = 0;
    for (int i = 0; supported_rates[i]; i++) {
      if (supported_rates[i] == expected_rates[n]) {
        found = 1;
      }
    }
    EXPECT_EQ(found, 1);
  }
  for (int n = 0; n < num_unexpected_rates; n++) {
    int found = 0;
    for (int i = 0; supported_rates[i]; i++) {
      if (supported_rates[i] == unexpected_rates[n]) {
        found = 1;
      }
    }
    EXPECT_EQ(found, 0);
  }
  for (int n = 0; n < num_expected_formats; n++) {
    int found = 0;
    for (int i = 0; supported_formats[i]; i++) {
      if (supported_formats[i] == expected_formats[n]) {
        found = 1;
      }
    }
    EXPECT_EQ(found, 1);
  }
  for (int n = 0; n < num_unexpected_formats; n++) {
    int found = 0;
    for (int i = 0; supported_formats[i]; i++) {
      if (supported_formats[i] == unexpected_formats[n]) {
        found = 1;
      }
    }
    EXPECT_EQ(found, 0);
  }
  for (int n = 0; n < num_expected_channel_counts; n++) {
    int found = 0;
    for (int i = 0; supported_channel_counts[i]; i++) {
      if (supported_channel_counts[i] == expected_channel_counts[n]) {
        found = 1;
      }
    }
    EXPECT_EQ(found, 1);
  }
  for (int n = 0; n < num_unexpected_channel_counts; n++) {
    int found = 0;
    for (int i = 0; supported_channel_counts[i]; i++) {
      if (supported_channel_counts[i] == unexpected_channel_counts[n]) {
        found = 1;
      }
    }
    EXPECT_EQ(found, 0);
  }
  free(supported_channel_counts);
  free(supported_rates);
  free(supported_formats);
}

TEST_F(A2dpManagerTestSuite, SetSupportAbsoluteVolume) {
  a2dp_pcm_iodev_create_ret =
      (struct cras_iodev*)calloc(1, sizeof(struct cras_iodev));
  struct cras_a2dp* a2dp =
      cras_floss_a2dp_create(NULL, "addr", "name", &a2dp_codecs);
  ASSERT_NE(a2dp, (struct cras_a2dp*)NULL);

  ASSERT_EQ(cras_floss_a2dp_get_support_absolute_volume(a2dp), false);

  cras_floss_a2dp_set_support_absolute_volume(a2dp, true);
  ASSERT_EQ(cras_floss_a2dp_get_support_absolute_volume(a2dp), true);
  ASSERT_EQ(a2dp_pcm_iodev_create_ret->software_volume_needed, 0);

  cras_floss_a2dp_set_support_absolute_volume(a2dp, false);
  ASSERT_EQ(cras_floss_a2dp_get_support_absolute_volume(a2dp), false);
  ASSERT_EQ(a2dp_pcm_iodev_create_ret->software_volume_needed, 1);

  cras_floss_a2dp_destroy(a2dp);
}

TEST_F(A2dpManagerTestSuite, ConvertVolume) {
  int volume;
  a2dp_pcm_iodev_create_ret =
      (struct cras_iodev*)calloc(1, sizeof(struct cras_iodev));
  struct cras_a2dp* a2dp =
      cras_floss_a2dp_create(NULL, "addr", "name", &a2dp_codecs);
  ASSERT_NE(a2dp, (struct cras_a2dp*)NULL);

  cras_floss_a2dp_set_support_absolute_volume(a2dp, true);
  volume = cras_floss_a2dp_convert_volume(a2dp, 127);
  ASSERT_EQ(volume, 100);

  volume = cras_floss_a2dp_convert_volume(a2dp, 100);
  ASSERT_EQ(volume, 78);

  volume = cras_floss_a2dp_convert_volume(a2dp, 150);
  ASSERT_EQ(volume, 100);

  cras_floss_a2dp_destroy(a2dp);
}

TEST_F(A2dpManagerTestSuite, SetVolume) {
  a2dp_pcm_iodev_create_ret =
      (struct cras_iodev*)calloc(1, sizeof(struct cras_iodev));
  struct cras_a2dp* a2dp =
      cras_floss_a2dp_create(NULL, "addr", "name", &a2dp_codecs);
  ASSERT_NE(a2dp, (struct cras_a2dp*)NULL);

  cras_floss_a2dp_set_volume(a2dp, 100);
  ASSERT_EQ(floss_media_a2dp_set_volume_called, 0);
  ASSERT_EQ(floss_media_a2dp_set_volume_arg, 0);

  cras_floss_a2dp_set_support_absolute_volume(a2dp, true);
  cras_floss_a2dp_set_volume(a2dp, 100);
  ASSERT_EQ(floss_media_a2dp_set_volume_called, 1);
  ASSERT_EQ(floss_media_a2dp_set_volume_arg, 127);

  cras_floss_a2dp_set_volume(a2dp, 50);
  ASSERT_EQ(floss_media_a2dp_set_volume_called, 2);
  ASSERT_EQ(floss_media_a2dp_set_volume_arg, 63);

  cras_floss_a2dp_destroy(a2dp);
}

TEST_F(A2dpManagerTestSuite, SuspendCallback) {
  struct cras_audio_format fmt;

  fmt.frame_rate = 44100;
  fmt.format = SND_PCM_FORMAT_S16_LE;
  fmt.num_channels = 2;

  a2dp_pcm_iodev_create_ret =
      (struct cras_iodev*)calloc(1, sizeof(struct cras_iodev));
  struct cras_a2dp* a2dp =
      cras_floss_a2dp_create(NULL, "addr", "name", &a2dp_codecs);
  ASSERT_NE(a2dp, (struct cras_a2dp*)NULL);

  EXPECT_EQ(0, cras_floss_a2dp_start(a2dp, &fmt));

  {
    CLEAR_AND_EVENTUALLY(EXPECT_EQ, cras_tm_create_timer_called, 1);

    cras_floss_a2dp_schedule_suspend(a2dp, 100, (enum A2DP_EXIT_CODE)0);
    cras_main_message_add_handler_callback(cras_main_message_send_msg,
                                           (void*)a2dp);
  }
  {
    CLEAR_AND_EVENTUALLY(EXPECT_EQ, floss_media_a2dp_suspend_called, 1);

    cras_tm_create_timer_cb(NULL, cras_tm_create_timer_cb_data);
  }
  {
    CLEAR_AND_EVENTUALLY(EXPECT_EQ, floss_media_a2dp_stop_audio_request_called,
                         1);
    cras_floss_a2dp_stop(a2dp);
  }

  cras_floss_a2dp_destroy(a2dp);
}
}  // namespace

extern "C" {
struct cras_bt_event_log* btlog;

// From cras_fl_pcm_iodev
struct cras_iodev* a2dp_pcm_iodev_create(struct cras_a2dp* a2dp,
                                         int sample_rates,
                                         int sample_sizes,
                                         int channel_modes) {
  a2dp_pcm_iodev_create_a2dp_val = a2dp;
  return a2dp_pcm_iodev_create_ret;
}

void a2dp_pcm_iodev_destroy(struct cras_iodev* iodev) {
  a2dp_pcm_iodev_destroy_iodev_val = iodev;
}

void a2dp_pcm_update_bt_stack_delay(struct cras_iodev* iodev,
                                    uint64_t total_bytes_read,
                                    uint64_t remote_delay_report_ns,
                                    struct timespec* data_position_ts) {
  a2dp_pcm_update_bt_stack_delay_called++;
  return;
}

int cras_main_message_send(struct cras_main_message* msg) {
  // cras_main_message is a local variable from caller, we should allocate
  // memory from heap and copy its data
  if (cras_main_message_send_msg) {
    free(cras_main_message_send_msg);
  }
  cras_main_message_send_msg =
      (struct cras_main_message*)calloc(1, msg->length);
  memcpy((void*)cras_main_message_send_msg, (void*)msg, msg->length);
  return 0;
}

int cras_main_message_add_handler(enum CRAS_MAIN_MESSAGE_TYPE type,
                                  cras_message_callback callback,
                                  void* callback_data) {
  cras_main_message_add_handler_callback = callback;
  cras_main_message_add_handler_callback_data = callback_data;
  return 0;
}

void cras_main_message_rm_handler(enum CRAS_MAIN_MESSAGE_TYPE type) {}

// From cras_system_state
struct cras_tm* cras_system_state_get_tm() {
  return NULL;
}

// socket and connect
int socket(int domain, int type, int protocol) {
  return fake_skt;
}

int connect(int sockfd, const struct sockaddr* addr, socklen_t addrlen) {
  return 0;
}

// From cras_tm
struct cras_timer* cras_tm_create_timer(struct cras_tm* tm,
                                        unsigned int ms,
                                        void (*cb)(struct cras_timer* t,
                                                   void* data),
                                        void* cb_data) {
  cras_tm_create_timer_called++;
  cras_tm_create_timer_cb = cb;
  cras_tm_create_timer_cb_data = cb_data;
  return cras_tm_create_timer_ret;
}

void cras_tm_cancel_timer(struct cras_tm* tm, struct cras_timer* t) {
  cras_tm_cancel_timer_called++;
  cras_tm_cancel_timer_arg = t;
}

// From fl_media
int floss_media_a2dp_set_active_device(struct fl_media* fm, const char* addr) {
  floss_media_a2dp_set_active_device_called++;
  return 0;
}
int floss_media_a2dp_set_audio_config(struct fl_media* fm,
                                      unsigned int rate,
                                      unsigned int bps,
                                      unsigned int channels) {
  floss_media_a2dp_set_audio_config_called++;
  floss_media_a2dp_set_audio_config_rate = rate;
  floss_media_a2dp_set_audio_config_bps = bps;
  floss_media_a2dp_set_audio_config_channels = channels;
  return 0;
}

int floss_media_a2dp_start_audio_request(struct fl_media* fm,
                                         const char* addr) {
  floss_media_a2dp_start_audio_request_called++;
  return 0;
}

int floss_media_a2dp_stop_audio_request(struct fl_media* fm) {
  floss_media_a2dp_stop_audio_request_called++;
  return 0;
}

int floss_media_a2dp_set_volume(struct fl_media* fm, unsigned int volume) {
  floss_media_a2dp_set_volume_called++;
  floss_media_a2dp_set_volume_arg = volume;
  return 0;
}

int floss_media_a2dp_get_presentation_position(
    struct fl_media* fm,
    uint64_t* remote_delay_report_ns,
    uint64_t* total_bytes_read,
    struct timespec* data_position_ts) {
  floss_media_a2dp_get_presentation_position_called++;
  return 0;
}

int floss_media_a2dp_suspend(struct fl_media* fm) {
  floss_media_a2dp_suspend_called++;
  return 0;
}

// From server metrics
int cras_server_metrics_a2dp_20ms_failure_over_stream(unsigned num) {
  return 0;
}

int cras_server_metrics_a2dp_100ms_failure_over_stream(unsigned num) {
  return 0;
}

int cras_server_metrics_a2dp_exit(enum A2DP_EXIT_CODE code) {
  return 0;
}

}  // extern "C"
