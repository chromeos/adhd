// Copyright 2018 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <gtest/gtest.h>
#include <stdio.h>
#include <unordered_map>

extern "C" {
#include "cras/src/server/audio_thread.h"
#include "cras/src/server/cras_apm_reverse.h"
#include "cras/src/server/cras_audio_area.h"
#include "cras/src/server/cras_iodev.h"
#include "cras/src/server/cras_iodev_list.h"
#include "cras/src/server/cras_main_message.h"
#include "cras/src/server/cras_processor_config.h"
#include "cras/src/server/cras_stream_apm.h"
#include "cras/src/server/float_buffer.h"
#include "cras_types.h"
#include "webrtc_apm.h"
}

#define FILENAME_TEMPLATE "ApmTest.XXXXXX"

namespace {

static struct cras_iodev devs[2];
static struct cras_iodev* idev = &devs[0];
static struct cras_iodev* idev2 = &devs[1];
static struct cras_stream_apm* stream;
static struct cras_audio_area fake_audio_area;
static unsigned int dsp_util_interleave_frames;
static unsigned int webrtc_apm_process_stream_f_called;
static unsigned int webrtc_apm_process_reverse_stream_f_called;
static int webrtc_apm_create_called;
static dictionary* webrtc_apm_create_aec_ini_val = NULL;
static dictionary* webrtc_apm_create_apm_ini_val = NULL;
static bool cras_apm_reverse_is_aec_use_case_ret;
static int cras_apm_reverse_state_update_called;
static int cras_apm_reverse_link_echo_ref_called;
static process_reverse_needed_t process_needed_cb_value;
static thread_callback thread_cb;
static void* cb_data;
static output_devices_changed_t output_devices_changed_callback = NULL;
static bool cras_iodev_is_tuned_aec_use_case_value;
static bool cras_iodev_is_dsp_aec_use_case_value;
static int cras_iodev_get_rtc_proc_enabled_called;
static int cras_iodev_set_rtc_proc_enabled_called;
static std::unordered_map<cras_iodev*, bool> iodev_rtc_proc_enabled_maps[3];
static unsigned int cras_main_message_send_called;
static std::vector<struct cras_stream_apm_message*> sent_apm_message_vector;

TEST(StreamApm, StreamApmCreate) {
  stream = cras_stream_apm_create(0);
  EXPECT_EQ((void*)NULL, stream);

  stream = cras_stream_apm_create(APM_ECHO_CANCELLATION);
  EXPECT_NE((void*)NULL, stream);
  EXPECT_EQ(APM_ECHO_CANCELLATION, cras_stream_apm_get_effects(stream));

  cras_stream_apm_destroy(stream);
}

static char* prepare_tempdir() {
  char dirname[sizeof(FILENAME_TEMPLATE) + 1];
  char filename[64];
  char* tempdir;
  FILE* fp;

  strcpy(dirname, FILENAME_TEMPLATE);
  tempdir = mkdtemp(dirname);
  snprintf(filename, 64, "%s/apm.ini", tempdir);
  fp = fopen(filename, "w");
  fprintf(fp, "%s", "[foo]\n");
  fclose(fp);
  fp = NULL;
  snprintf(filename, 64, "%s/aec.ini", tempdir);
  fp = fopen(filename, "w");
  fprintf(fp, "%s", "[bar]\n");
  fclose(fp);
  fp = NULL;
  return strdup(tempdir);
}

static void delete_tempdir(char* dir) {
  char filename[64];

  snprintf(filename, 64, "%s/apm.ini", dir);
  unlink(filename);
  snprintf(filename, 64, "%s/aec.ini", dir);
  unlink(filename);
  rmdir(dir);
}

static void init_channel_layout(struct cras_audio_format* fmt) {
  int i;
  for (i = 0; i < CRAS_CH_MAX; i++) {
    fmt->channel_layout[i] = -1;
  }
}

TEST(StreamApm, AddApmInputDevUnuseFirstChannel) {
  struct cras_audio_format fmt;
  struct cras_audio_format* val;
  struct cras_apm* apm;
  int ch;
  const int num_test_casts = 9;
  int test_layouts[num_test_casts][CRAS_CH_MAX] = {
      {0, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
      {0, 0, -1, -1, -1, -1, -1, -1, -1, -1, -1},
      {0, 1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
      {1, 1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
      {1, 0, -1, -1, -1, -1, -1, -1, -1, -1, -1},
      {2, 2, -1, -1, -1, -1, -1, -1, -1, -1, -1},
      {2, 3, -1, -1, -1, -1, -1, -1, -1, -1, -1},
      {3, 3, -1, -1, -1, -1, -1, -1, -1, -1, -1},
      {3, 2, -1, -1, -1, -1, -1, -1, -1, -1, -1}};
  int test_num_channels[num_test_casts] = {1, 2, 2, 2, 2, 3, 4, 4, 4};

  fmt.frame_rate = 48000;
  fmt.format = SND_PCM_FORMAT_S16_LE;
  cras_iodev_is_tuned_aec_use_case_value = 1;
  cras_iodev_is_dsp_aec_use_case_value = 1;

  cras_stream_apm_init("");
  stream = cras_stream_apm_create(APM_ECHO_CANCELLATION);
  EXPECT_NE((void*)NULL, stream);

  for (int i = 0; i < num_test_casts; i++) {
    fmt.num_channels = test_num_channels[i];
    init_channel_layout(&fmt);
    for (ch = 0; ch < CRAS_CH_MAX; ch++) {
      fmt.channel_layout[ch] = test_layouts[i][ch];
    }

    // Input dev is of aec use case.
    apm = cras_stream_apm_add(stream, idev, &fmt);
    EXPECT_NE((void*)NULL, apm);

    /* Assert that the post-processing format never has an unset
     * first channel in the layout. */
    bool first_channel_found_in_layout = 0;
    val = cras_stream_apm_get_format(apm);
    for (ch = 0; ch < CRAS_CH_MAX; ch++) {
      if (0 == val->channel_layout[ch]) {
        first_channel_found_in_layout = 1;
      }
    }

    EXPECT_EQ(1, first_channel_found_in_layout);

    cras_stream_apm_remove(stream, idev);
  }

  cras_stream_apm_destroy(stream);
  cras_stream_apm_deinit();
}

TEST(StreamApm, AddRemoveApm) {
  struct cras_audio_format fmt;
  char* dir;

  fmt.num_channels = 2;
  fmt.frame_rate = 48000;
  fmt.format = SND_PCM_FORMAT_S16_LE;

  dir = prepare_tempdir();
  cras_stream_apm_init(dir);
  cras_apm_reverse_is_aec_use_case_ret = 1;
  cras_apm_reverse_state_update_called = 0;
  cras_iodev_is_tuned_aec_use_case_value = 1;
  cras_iodev_is_dsp_aec_use_case_value = 1;

  stream = cras_stream_apm_create(APM_ECHO_CANCELLATION);
  EXPECT_NE((void*)NULL, stream);

  // Input dev is of aec use case.
  EXPECT_NE((void*)NULL, cras_stream_apm_add(stream, idev, &fmt));
  EXPECT_NE((void*)NULL, webrtc_apm_create_aec_ini_val);
  EXPECT_NE((void*)NULL, webrtc_apm_create_apm_ini_val);
  EXPECT_EQ((void*)NULL, cras_stream_apm_get_active(stream, idev));
  EXPECT_EQ(0, cras_apm_reverse_state_update_called);

  cras_stream_apm_start(stream, idev);
  EXPECT_NE((void*)NULL, cras_stream_apm_get_active(stream, idev));
  EXPECT_EQ((void*)NULL, cras_stream_apm_get_active(stream, idev2));
  EXPECT_EQ(1, cras_apm_reverse_state_update_called);

  // Input dev is not of aec use case.
  cras_iodev_is_tuned_aec_use_case_value = 0;
  EXPECT_NE((void*)NULL, cras_stream_apm_add(stream, idev2, &fmt));
  EXPECT_EQ((void*)NULL, webrtc_apm_create_aec_ini_val);
  EXPECT_EQ((void*)NULL, webrtc_apm_create_apm_ini_val);
  EXPECT_EQ(1, cras_apm_reverse_state_update_called);
  cras_stream_apm_start(stream, idev2);
  EXPECT_EQ(2, cras_apm_reverse_state_update_called);
  cras_stream_apm_stop(stream, idev);
  EXPECT_EQ(3, cras_apm_reverse_state_update_called);

  EXPECT_EQ((void*)NULL, cras_stream_apm_get_active(stream, idev));
  EXPECT_NE((void*)NULL, cras_stream_apm_get_active(stream, idev2));

  cras_stream_apm_stop(stream, idev2);
  cras_stream_apm_remove(stream, idev);
  cras_stream_apm_remove(stream, idev2);

  cras_stream_apm_destroy(stream);
  cras_stream_apm_deinit();
  delete_tempdir(dir);
  free(dir);
}

TEST(StreamApm, OutputTypeNotAecUseCase) {
  struct cras_audio_format fmt;
  char* dir;

  fmt.num_channels = 2;
  fmt.frame_rate = 48000;
  fmt.format = SND_PCM_FORMAT_S16_LE;
  cras_iodev_is_tuned_aec_use_case_value = 1;
  cras_iodev_is_dsp_aec_use_case_value = 1;

  dir = prepare_tempdir();
  cras_stream_apm_init(dir);

  stream = cras_stream_apm_create(APM_ECHO_CANCELLATION);
  EXPECT_NE((void*)NULL, stream);

  // Output device is of aec use case.
  cras_apm_reverse_is_aec_use_case_ret = 1;
  EXPECT_NE((void*)NULL, cras_stream_apm_add(stream, idev, &fmt));
  EXPECT_NE((void*)NULL, webrtc_apm_create_aec_ini_val);
  EXPECT_NE((void*)NULL, webrtc_apm_create_apm_ini_val);
  cras_stream_apm_remove(stream, idev);

  // Output device is not of aec use case.
  cras_apm_reverse_is_aec_use_case_ret = 0;
  EXPECT_NE((void*)NULL, cras_stream_apm_add(stream, idev, &fmt));
  EXPECT_EQ((void*)NULL, webrtc_apm_create_aec_ini_val);
  EXPECT_EQ((void*)NULL, webrtc_apm_create_apm_ini_val);
  cras_stream_apm_remove(stream, idev);

  cras_stream_apm_destroy(stream);
  cras_stream_apm_deinit();
  delete_tempdir(dir);
  free(dir);
}

TEST(StreamApm, ApmProcessForwardBuffer) {
  struct cras_apm* apm;
  struct cras_audio_format fmt;
  struct cras_audio_area* area;
  struct float_buffer* buf;

  fmt.num_channels = 2;
  fmt.frame_rate = 48000;
  fmt.format = SND_PCM_FORMAT_S16_LE;
  init_channel_layout(&fmt);
  fmt.channel_layout[CRAS_CH_FL] = 0;
  fmt.channel_layout[CRAS_CH_FR] = 1;
  cras_iodev_is_tuned_aec_use_case_value = 1;
  cras_iodev_is_dsp_aec_use_case_value = 1;

  cras_stream_apm_init("");

  stream = cras_stream_apm_create(APM_ECHO_CANCELLATION);
  EXPECT_NE((void*)NULL, stream);

  apm = cras_stream_apm_add(stream, idev, &fmt);

  buf = float_buffer_create(500, 2);
  float_buffer_written(buf, 300);
  webrtc_apm_process_stream_f_called = 0;
  cras_stream_apm_process(apm, buf, 0, 1);
  EXPECT_EQ(0, webrtc_apm_process_stream_f_called);

  area = cras_stream_apm_get_processed(apm);
  EXPECT_EQ(0, area->frames);

  float_buffer_reset(buf);
  float_buffer_written(buf, 200);
  cras_stream_apm_process(apm, buf, 0, 1);
  area = cras_stream_apm_get_processed(apm);
  EXPECT_EQ(1, webrtc_apm_process_stream_f_called);
  EXPECT_EQ(480, dsp_util_interleave_frames);
  EXPECT_EQ(480, area->frames);

  /* Put some processed frames. Another stream_apm process will not call
   * into webrtc_apm because the processed buffer is not yet empty.
   */
  cras_stream_apm_put_processed(apm, 200);
  float_buffer_reset(buf);
  float_buffer_written(buf, 500);
  cras_stream_apm_process(apm, buf, 0, 1);
  EXPECT_EQ(1, webrtc_apm_process_stream_f_called);

  /* Put another 280 processed frames, so it's now ready for webrtc_apm
   * to process another chunk of 480 frames (10ms) data.
   */
  cras_stream_apm_put_processed(apm, 280);
  cras_stream_apm_process(apm, buf, 0, 1);
  EXPECT_EQ(2, webrtc_apm_process_stream_f_called);

  float_buffer_destroy(&buf);
  cras_stream_apm_destroy(stream);
  cras_stream_apm_deinit();
}

TEST(StreamApm, StreamAddToAlreadyOpenedDev) {
  struct cras_audio_format fmt;
  struct cras_apm *apm1, *apm2;

  fmt.num_channels = 2;
  fmt.frame_rate = 48000;
  fmt.format = SND_PCM_FORMAT_S16_LE;
  cras_iodev_is_tuned_aec_use_case_value = 1;
  cras_iodev_is_dsp_aec_use_case_value = 1;
  cras_stream_apm_init("");

  webrtc_apm_create_called = 0;
  stream = cras_stream_apm_create(APM_ECHO_CANCELLATION);
  EXPECT_NE((void*)NULL, stream);

  apm1 = cras_stream_apm_add(stream, idev, &fmt);
  EXPECT_EQ(1, webrtc_apm_create_called);
  EXPECT_NE((void*)NULL, apm1);

  apm2 = cras_stream_apm_add(stream, idev, &fmt);
  EXPECT_EQ(1, webrtc_apm_create_called);
  EXPECT_EQ(apm1, apm2);

  cras_stream_apm_destroy(stream);
  cras_stream_apm_deinit();
}

TEST(StreamApm, ReverseDevChanged) {
  cras_stream_apm_init("");
  EXPECT_NE((void*)NULL, output_devices_changed_callback);
  EXPECT_NE((void*)NULL, thread_cb);

  cras_apm_reverse_state_update_called = 0;
  output_devices_changed_callback();
  EXPECT_EQ(0, cras_apm_reverse_state_update_called);
  thread_cb(cb_data, POLLIN);
  EXPECT_EQ(1, cras_apm_reverse_state_update_called);

  cras_stream_apm_deinit();
}

TEST(StreamApm, GetUseTunedSettings) {
  struct cras_audio_format fmt;
  char* dir;

  fmt.num_channels = 2;
  fmt.frame_rate = 48000;
  fmt.format = SND_PCM_FORMAT_S16_LE;

  // No tuned aec/apm ini provided.
  cras_stream_apm_init("");

  stream = cras_stream_apm_create(APM_ECHO_CANCELLATION);
  cras_stream_apm_add(stream, idev, &fmt);
  cras_stream_apm_start(stream, idev);

  cras_iodev_is_tuned_aec_use_case_value = 1;
  cras_iodev_is_dsp_aec_use_case_value = 0;
  cras_apm_reverse_is_aec_use_case_ret = 1;
  EXPECT_EQ(false, cras_stream_apm_get_use_tuned_settings(stream, idev));

  cras_stream_apm_stop(stream, idev);
  cras_stream_apm_remove(stream, idev);
  cras_stream_apm_destroy(stream);
  cras_stream_apm_deinit();

  // Fake the existence of tuned apm/aec ini.
  dir = prepare_tempdir();
  cras_stream_apm_init(dir);

  stream = cras_stream_apm_create(APM_ECHO_CANCELLATION);
  cras_stream_apm_add(stream, idev, &fmt);
  cras_stream_apm_start(stream, idev);

  cras_iodev_is_tuned_aec_use_case_value = 1;
  cras_apm_reverse_is_aec_use_case_ret = 1;
  EXPECT_EQ(true, cras_stream_apm_get_use_tuned_settings(stream, idev));

  cras_iodev_is_tuned_aec_use_case_value = 0;
  EXPECT_EQ(false, cras_stream_apm_get_use_tuned_settings(stream, idev));

  cras_iodev_is_tuned_aec_use_case_value = 1;
  cras_apm_reverse_is_aec_use_case_ret = 0;
  EXPECT_EQ(false, cras_stream_apm_get_use_tuned_settings(stream, idev));

  cras_stream_apm_stop(stream, idev);
  cras_stream_apm_remove(stream, idev);
  cras_stream_apm_destroy(stream);
  cras_stream_apm_deinit();
  delete_tempdir(dir);
  free(dir);
}

TEST(ApmList, NeedsReverseProcessing) {
  struct cras_stream_apm* stream2;
  struct cras_audio_format fmt;
  struct cras_iodev *output1, *output2;

  fmt.num_channels = 2;
  fmt.frame_rate = 48000;
  fmt.format = SND_PCM_FORMAT_S16_LE;

  cras_apm_reverse_link_echo_ref_called = 0;
  cras_apm_reverse_state_update_called = 0;
  cras_iodev_is_tuned_aec_use_case_value = 1;
  cras_iodev_is_dsp_aec_use_case_value = 1;
  cras_stream_apm_init("");

  stream = cras_stream_apm_create(APM_ECHO_CANCELLATION);
  EXPECT_NE((void*)NULL, stream);
  stream2 = cras_stream_apm_create(APM_ECHO_CANCELLATION);
  EXPECT_NE((void*)NULL, stream2);

  cras_stream_apm_add(stream, idev, &fmt);
  cras_stream_apm_start(stream, idev);
  cras_stream_apm_add(stream2, idev, &fmt);
  cras_stream_apm_start(stream2, idev);
  EXPECT_EQ(2, cras_apm_reverse_state_update_called);

  output1 = reinterpret_cast<struct cras_iodev*>(0x654);
  EXPECT_EQ(1, process_needed_cb_value(1, output1));

  output2 = reinterpret_cast<struct cras_iodev*>(0x321);
  EXPECT_EQ(0, process_needed_cb_value(0, output2));

  /* Set aec ref to output2, expect reverse process is needed for
   * non-default |output2|. */
  cras_stream_apm_set_aec_ref(stream, output2);
  EXPECT_EQ(1, process_needed_cb_value(0, output2));
  thread_cb(cb_data, POLLIN);
  EXPECT_EQ(1, cras_apm_reverse_link_echo_ref_called);
  EXPECT_EQ(3, cras_apm_reverse_state_update_called);

  /* Process reverse is needed for default |output1| because there's still
   * the |stream2| tracking default output. */
  EXPECT_EQ(1, process_needed_cb_value(1, output1));

  /* Set streamist back to track default output as aec ref. Expect reverse
   * process is no longer needed on |output2|. */
  cras_stream_apm_set_aec_ref(stream, NULL);
  EXPECT_EQ(0, process_needed_cb_value(0, output2));
  thread_cb(cb_data, POLLIN);
  EXPECT_EQ(2, cras_apm_reverse_link_echo_ref_called);
  EXPECT_EQ(4, cras_apm_reverse_state_update_called);

  /* Assume the default output now changes to output2. Expect reverse process
   * is needed, because |stream| is tracking default. And |output1| is not
   * needed because no one is tracking it as aec ref. */
  EXPECT_EQ(1, process_needed_cb_value(1, output2));
  EXPECT_EQ(0, process_needed_cb_value(0, output1));

  cras_stream_apm_stop(stream, idev);
  cras_stream_apm_stop(stream2, idev);

  cras_stream_apm_remove(stream, idev);
  cras_stream_apm_remove(stream2, idev);

  cras_stream_apm_destroy(stream);
  EXPECT_EQ(3, cras_apm_reverse_link_echo_ref_called);
  cras_stream_apm_destroy(stream2);
  EXPECT_EQ(4, cras_apm_reverse_link_echo_ref_called);
  cras_stream_apm_deinit();
}

TEST(StreamApm, DSPEffectsNotSupportedShouldNotCallIodevOps) {
  struct cras_audio_format fmt;
  struct cras_apm* apm1;

  fmt.num_channels = 2;
  fmt.frame_rate = 48000;
  fmt.format = SND_PCM_FORMAT_S16_LE;

  cras_iodev_is_tuned_aec_use_case_value = 1;
  cras_iodev_is_dsp_aec_use_case_value = 0;
  cras_iodev_get_rtc_proc_enabled_called = 0;
  cras_iodev_set_rtc_proc_enabled_called = 0;
  iodev_rtc_proc_enabled_maps[RTC_PROC_AEC].clear();
  iodev_rtc_proc_enabled_maps[RTC_PROC_NS].clear();
  iodev_rtc_proc_enabled_maps[RTC_PROC_AGC].clear();
  cras_stream_apm_init("");

  stream = cras_stream_apm_create(APM_ECHO_CANCELLATION | APM_NOISE_SUPRESSION |
                                  APM_GAIN_CONTROL);
  EXPECT_NE((void*)NULL, stream);

  apm1 = cras_stream_apm_add(stream, idev, &fmt);
  EXPECT_NE((void*)NULL, apm1);
  cras_stream_apm_start(stream, idev);
  EXPECT_EQ(0, cras_iodev_set_rtc_proc_enabled_called);

  cras_stream_apm_stop(stream, idev);
  EXPECT_EQ(0, cras_iodev_set_rtc_proc_enabled_called);

  cras_stream_apm_remove(stream, idev);
  cras_stream_apm_destroy(stream);
  cras_stream_apm_deinit();
}

TEST(StreamApm, UpdateEffect) {
  struct cras_audio_format fmt;
  struct cras_apm* apm1;

  fmt.num_channels = 2;
  fmt.frame_rate = 48000;
  fmt.format = SND_PCM_FORMAT_S16_LE;

  cras_iodev_is_tuned_aec_use_case_value = 1;
  cras_iodev_is_dsp_aec_use_case_value = 1;
  cras_apm_reverse_is_aec_use_case_ret = 1;
  cras_iodev_get_rtc_proc_enabled_called = 0;
  cras_iodev_set_rtc_proc_enabled_called = 0;
  iodev_rtc_proc_enabled_maps[RTC_PROC_AEC].clear();
  iodev_rtc_proc_enabled_maps[RTC_PROC_NS].clear();
  iodev_rtc_proc_enabled_maps[RTC_PROC_AGC].clear();
  cras_stream_apm_init("");

  stream = cras_stream_apm_create(APM_ECHO_CANCELLATION |
                                  DSP_ECHO_CANCELLATION_ALLOWED);
  EXPECT_NE((void*)NULL, stream);

  apm1 = cras_stream_apm_add(stream, idev, &fmt);
  EXPECT_NE((void*)NULL, apm1);
  cras_stream_apm_start(stream, idev);
  EXPECT_EQ(true, iodev_rtc_proc_enabled_maps[RTC_PROC_AEC][idev]);
  EXPECT_EQ(false, iodev_rtc_proc_enabled_maps[RTC_PROC_NS][idev]);
  EXPECT_EQ(false, iodev_rtc_proc_enabled_maps[RTC_PROC_AGC][idev]);
  cras_stream_apm_stop(stream, idev);
  EXPECT_EQ(false, iodev_rtc_proc_enabled_maps[RTC_PROC_AEC][idev]);
  EXPECT_EQ(false, iodev_rtc_proc_enabled_maps[RTC_PROC_NS][idev]);
  EXPECT_EQ(false, iodev_rtc_proc_enabled_maps[RTC_PROC_AGC][idev]);
  cras_stream_apm_remove(stream, idev);
  cras_stream_apm_destroy(stream);

  // No DSP aec allowed should block DSP ns/agc being enabled.
  stream = cras_stream_apm_create(
      APM_ECHO_CANCELLATION | APM_NOISE_SUPRESSION | APM_GAIN_CONTROL |
      DSP_NOISE_SUPPRESSION_ALLOWED | DSP_GAIN_CONTROL_ALLOWED);
  EXPECT_NE((void*)NULL, stream);

  apm1 = cras_stream_apm_add(stream, idev, &fmt);
  EXPECT_NE((void*)NULL, apm1);
  cras_stream_apm_start(stream, idev);
  EXPECT_EQ(false, iodev_rtc_proc_enabled_maps[RTC_PROC_AEC][idev]);
  EXPECT_EQ(false, iodev_rtc_proc_enabled_maps[RTC_PROC_NS][idev]);
  EXPECT_EQ(false, iodev_rtc_proc_enabled_maps[RTC_PROC_AGC][idev]);
  cras_stream_apm_stop(stream, idev);
  EXPECT_EQ(false, iodev_rtc_proc_enabled_maps[RTC_PROC_AEC][idev]);
  EXPECT_EQ(false, iodev_rtc_proc_enabled_maps[RTC_PROC_NS][idev]);
  EXPECT_EQ(false, iodev_rtc_proc_enabled_maps[RTC_PROC_AGC][idev]);
  cras_stream_apm_remove(stream, idev);
  cras_stream_apm_destroy(stream);

  // Allowing DSP aec means DSP ns/agc can be enalbed.
  stream = cras_stream_apm_create(
      APM_ECHO_CANCELLATION | APM_NOISE_SUPRESSION | APM_GAIN_CONTROL |
      DSP_ECHO_CANCELLATION_ALLOWED | DSP_NOISE_SUPPRESSION_ALLOWED |
      DSP_GAIN_CONTROL_ALLOWED);
  EXPECT_NE((void*)NULL, stream);

  apm1 = cras_stream_apm_add(stream, idev, &fmt);
  EXPECT_NE((void*)NULL, apm1);
  cras_stream_apm_start(stream, idev);
  EXPECT_EQ(true, iodev_rtc_proc_enabled_maps[RTC_PROC_AEC][idev]);
  EXPECT_EQ(true, iodev_rtc_proc_enabled_maps[RTC_PROC_NS][idev]);
  EXPECT_EQ(true, iodev_rtc_proc_enabled_maps[RTC_PROC_AGC][idev]);
  cras_stream_apm_stop(stream, idev);
  EXPECT_EQ(false, iodev_rtc_proc_enabled_maps[RTC_PROC_AEC][idev]);
  EXPECT_EQ(false, iodev_rtc_proc_enabled_maps[RTC_PROC_NS][idev]);
  EXPECT_EQ(false, iodev_rtc_proc_enabled_maps[RTC_PROC_AGC][idev]);
  cras_stream_apm_remove(stream, idev);
  cras_stream_apm_destroy(stream);

  // Add apm with tuned aec use case set to 'false' blocks DSP effects.
  stream = cras_stream_apm_create(
      APM_ECHO_CANCELLATION | APM_NOISE_SUPRESSION | APM_GAIN_CONTROL |
      DSP_ECHO_CANCELLATION_ALLOWED | DSP_NOISE_SUPPRESSION_ALLOWED |
      DSP_GAIN_CONTROL_ALLOWED);
  EXPECT_NE((void*)NULL, stream);

  cras_iodev_is_tuned_aec_use_case_value = 0;
  cras_iodev_is_dsp_aec_use_case_value = 0;
  apm1 = cras_stream_apm_add(stream, idev, &fmt);
  EXPECT_NE((void*)NULL, apm1);
  cras_stream_apm_start(stream, idev);
  EXPECT_EQ(false, iodev_rtc_proc_enabled_maps[RTC_PROC_AEC][idev]);
  EXPECT_EQ(false, iodev_rtc_proc_enabled_maps[RTC_PROC_NS][idev]);
  EXPECT_EQ(false, iodev_rtc_proc_enabled_maps[RTC_PROC_AGC][idev]);
  cras_stream_apm_stop(stream, idev);
  EXPECT_EQ(false, iodev_rtc_proc_enabled_maps[RTC_PROC_AEC][idev]);
  EXPECT_EQ(false, iodev_rtc_proc_enabled_maps[RTC_PROC_NS][idev]);
  EXPECT_EQ(false, iodev_rtc_proc_enabled_maps[RTC_PROC_AGC][idev]);
  cras_stream_apm_remove(stream, idev);
  cras_stream_apm_destroy(stream);

  // Add apm with dsp aec use case set to 'false' blocks DSP effects.
  stream = cras_stream_apm_create(
      APM_ECHO_CANCELLATION | APM_NOISE_SUPRESSION | APM_GAIN_CONTROL |
      DSP_ECHO_CANCELLATION_ALLOWED | DSP_NOISE_SUPPRESSION_ALLOWED |
      DSP_GAIN_CONTROL_ALLOWED);
  EXPECT_NE((void*)NULL, stream);

  cras_iodev_is_tuned_aec_use_case_value = 1;
  cras_iodev_is_dsp_aec_use_case_value = 0;
  apm1 = cras_stream_apm_add(stream, idev, &fmt);
  EXPECT_NE((void*)NULL, apm1);
  cras_stream_apm_start(stream, idev);
  EXPECT_EQ(false, iodev_rtc_proc_enabled_maps[RTC_PROC_AEC][idev]);
  EXPECT_EQ(false, iodev_rtc_proc_enabled_maps[RTC_PROC_NS][idev]);
  EXPECT_EQ(false, iodev_rtc_proc_enabled_maps[RTC_PROC_AGC][idev]);
  cras_stream_apm_stop(stream, idev);
  EXPECT_EQ(false, iodev_rtc_proc_enabled_maps[RTC_PROC_AEC][idev]);
  EXPECT_EQ(false, iodev_rtc_proc_enabled_maps[RTC_PROC_NS][idev]);
  EXPECT_EQ(false, iodev_rtc_proc_enabled_maps[RTC_PROC_AGC][idev]);
  cras_stream_apm_remove(stream, idev);
  cras_stream_apm_destroy(stream);

  cras_stream_apm_deinit();
}

TEST(StreamApm, UpdateEffectMultipleStreamApms) {
  struct cras_audio_format fmt;
  struct cras_apm *apm1, *apm2;
  struct cras_stream_apm* stream2;

  fmt.num_channels = 2;
  fmt.frame_rate = 48000;
  fmt.format = SND_PCM_FORMAT_S16_LE;

  cras_iodev_is_tuned_aec_use_case_value = 1;
  cras_iodev_is_dsp_aec_use_case_value = 1;
  cras_apm_reverse_is_aec_use_case_ret = 1;
  cras_iodev_get_rtc_proc_enabled_called = 0;
  cras_iodev_set_rtc_proc_enabled_called = 0;
  iodev_rtc_proc_enabled_maps[RTC_PROC_AEC].clear();
  iodev_rtc_proc_enabled_maps[RTC_PROC_NS].clear();
  iodev_rtc_proc_enabled_maps[RTC_PROC_AGC].clear();
  cras_stream_apm_init("");

  // Allowing DSP aec means DSP ns/agc can be enalbed.
  stream = cras_stream_apm_create(
      APM_ECHO_CANCELLATION | APM_NOISE_SUPRESSION | APM_GAIN_CONTROL |
      DSP_ECHO_CANCELLATION_ALLOWED | DSP_NOISE_SUPPRESSION_ALLOWED |
      DSP_GAIN_CONTROL_ALLOWED);
  EXPECT_NE((void*)NULL, stream);

  apm1 = cras_stream_apm_add(stream, idev, &fmt);
  EXPECT_NE((void*)NULL, apm1);
  cras_stream_apm_start(stream, idev);
  EXPECT_EQ(true, iodev_rtc_proc_enabled_maps[RTC_PROC_AEC][idev]);
  EXPECT_EQ(true, iodev_rtc_proc_enabled_maps[RTC_PROC_NS][idev]);
  EXPECT_EQ(true, iodev_rtc_proc_enabled_maps[RTC_PROC_AGC][idev]);

  /* Another stream apm not feasible to use with DSP effect would
   * block enabling DSP effect on |idev|. */
  stream2 = cras_stream_apm_create(APM_ECHO_CANCELLATION);
  EXPECT_NE((void*)NULL, stream);
  apm2 = cras_stream_apm_add(stream2, idev, &fmt);
  EXPECT_NE((void*)NULL, apm2);
  cras_stream_apm_start(stream2, idev);
  EXPECT_EQ(false, iodev_rtc_proc_enabled_maps[RTC_PROC_AEC][idev]);
  EXPECT_EQ(false, iodev_rtc_proc_enabled_maps[RTC_PROC_NS][idev]);
  EXPECT_EQ(false, iodev_rtc_proc_enabled_maps[RTC_PROC_AGC][idev]);

  cras_stream_apm_stop(stream2, idev);
  EXPECT_EQ(true, iodev_rtc_proc_enabled_maps[RTC_PROC_AEC][idev]);
  EXPECT_EQ(true, iodev_rtc_proc_enabled_maps[RTC_PROC_NS][idev]);
  EXPECT_EQ(true, iodev_rtc_proc_enabled_maps[RTC_PROC_AGC][idev]);
  cras_stream_apm_remove(stream2, idev);
  cras_stream_apm_destroy(stream2);

  /* Another stream apm not feasible to use with DSP effect does not
   * cause a problem when it's added on a different iodbv
   * (i.e idev2 in this case. */
  stream2 = cras_stream_apm_create(APM_ECHO_CANCELLATION);
  EXPECT_NE((void*)NULL, stream);
  apm2 = cras_stream_apm_add(stream2, idev2, &fmt);
  EXPECT_NE((void*)NULL, apm2);
  cras_stream_apm_start(stream2, idev);
  EXPECT_EQ(true, iodev_rtc_proc_enabled_maps[RTC_PROC_AEC][idev]);
  EXPECT_EQ(true, iodev_rtc_proc_enabled_maps[RTC_PROC_NS][idev]);
  EXPECT_EQ(true, iodev_rtc_proc_enabled_maps[RTC_PROC_AGC][idev]);
  cras_stream_apm_stop(stream2, idev);
  EXPECT_EQ(true, iodev_rtc_proc_enabled_maps[RTC_PROC_AEC][idev]);
  EXPECT_EQ(true, iodev_rtc_proc_enabled_maps[RTC_PROC_NS][idev]);
  EXPECT_EQ(true, iodev_rtc_proc_enabled_maps[RTC_PROC_AGC][idev]);
  cras_stream_apm_remove(stream2, idev);
  cras_stream_apm_destroy(stream2);

  cras_stream_apm_stop(stream, idev);
  EXPECT_EQ(false, iodev_rtc_proc_enabled_maps[RTC_PROC_AEC][idev]);
  EXPECT_EQ(false, iodev_rtc_proc_enabled_maps[RTC_PROC_NS][idev]);
  EXPECT_EQ(false, iodev_rtc_proc_enabled_maps[RTC_PROC_AGC][idev]);
  cras_stream_apm_remove(stream, idev);
  cras_stream_apm_destroy(stream);

  cras_stream_apm_deinit();
}

extern "C" {
void audio_thread_add_events_callback(int fd,
                                      thread_callback cb,
                                      void* data,
                                      int events) {
  thread_cb = cb;
  cb_data = data;
  return;
}
int audio_thread_rm_callback_sync(struct audio_thread* thread, int fd) {
  thread_cb = NULL;
  cb_data = NULL;
  return 0;
}
void audio_thread_rm_callback(int fd) {}

struct audio_thread* cras_iodev_list_get_audio_thread() {
  return NULL;
}
void cras_iodev_list_reconnect_streams_with_apm() {}

bool cras_iodev_is_tuned_aec_use_case(const struct cras_ionode* node) {
  return cras_iodev_is_tuned_aec_use_case_value;
}
bool cras_iodev_is_dsp_aec_use_case(const struct cras_ionode* node) {
  return cras_iodev_is_dsp_aec_use_case_value;
}
bool cras_iodev_set_rtc_proc_enabled(struct cras_iodev* iodev,
                                     enum RTC_PROC_ON_DSP rtc_proc,
                                     bool enabled) {
  iodev_rtc_proc_enabled_maps[rtc_proc][iodev] = enabled;
  cras_iodev_set_rtc_proc_enabled_called++;
  return 0;
}
bool cras_iodev_get_rtc_proc_enabled(struct cras_iodev* iodev,
                                     enum RTC_PROC_ON_DSP rtc_proc) {
  cras_iodev_get_rtc_proc_enabled_called++;

  auto elem = iodev_rtc_proc_enabled_maps[rtc_proc].find(iodev);
  if (elem != iodev_rtc_proc_enabled_maps[rtc_proc].end()) {
    return iodev_rtc_proc_enabled_maps[rtc_proc][iodev];
  }

  return 0;
}

struct cras_audio_area* cras_audio_area_create(int num_channels) {
  return &fake_audio_area;
}

void cras_audio_area_destroy(struct cras_audio_area* area) {}
void cras_audio_area_config_channels(struct cras_audio_area* area,
                                     const struct cras_audio_format* fmt) {}
void cras_audio_area_config_buf_pointers(struct cras_audio_area* area,
                                         const struct cras_audio_format* fmt,
                                         uint8_t* base_buffer) {}
void dsp_util_interleave(float* const* input,
                         int16_t* output,
                         int channels,
                         snd_pcm_format_t format,
                         int frames) {
  dsp_util_interleave_frames = frames;
}
struct aec_config* aec_config_get(const char* device_config_dir) {
  return NULL;
}
void aec_config_dump(struct aec_config* config) {}
struct apm_config* apm_config_get(const char* device_config_dir) {
  return NULL;
}
void apm_config_dump(struct apm_config* config) {}
void webrtc_apm_init_metrics(const char* prefix) {}
webrtc_apm webrtc_apm_create_with_enforced_effects(
    unsigned int num_channels,
    unsigned int frame_rate,
    dictionary* aec_ini,
    dictionary* apm_ini,
    unsigned int enforce_aec_on,
    unsigned int enforce_ns_on,
    unsigned int enforce_agc_on) {
  webrtc_apm_create_called++;
  webrtc_apm_create_aec_ini_val = aec_ini;
  webrtc_apm_create_apm_ini_val = apm_ini;
  return reinterpret_cast<webrtc_apm>(0x11);
}
void webrtc_apm_dump_configs(dictionary* aec_ini, dictionary* apm_ini) {}
void webrtc_apm_destroy(webrtc_apm apm) {
  return;
}
int webrtc_apm_process_stream_f(webrtc_apm ptr,
                                int num_channels,
                                int rate,
                                float* const* data) {
  webrtc_apm_process_stream_f_called++;
  return 0;
}

int webrtc_apm_process_reverse_stream_f(webrtc_apm ptr,
                                        int num_channels,
                                        int rate,
                                        float* const* data) {
  webrtc_apm_process_reverse_stream_f_called++;
  return 0;
}
int webrtc_apm_aec_dump(webrtc_apm ptr,
                        void** work_queue,
                        int start,
                        FILE* handle) {
  return 0;
}
void webrtc_apm_enable_effects(webrtc_apm ptr,
                               bool enable_aec,
                               bool enable_ns,
                               bool enable_agc) {}

void webrtc_apm_enable_vad(webrtc_apm ptr, bool enable_vad) {}

int webrtc_apm_get_voice_detected(webrtc_apm ptr) {
  return false;
}

int cras_apm_reverse_init(process_reverse_t process_cb,
                          process_reverse_needed_t process_needed_cb,
                          output_devices_changed_t output_devices_changed_cb) {
  process_needed_cb_value = process_needed_cb;
  output_devices_changed_callback = output_devices_changed_cb;
  return 0;
}

void cras_apm_reverse_state_update() {
  cras_apm_reverse_state_update_called++;
}

int cras_apm_reverse_link_echo_ref(struct cras_stream_apm* stream,
                                   struct cras_iodev* echo_ref) {
  cras_apm_reverse_link_echo_ref_called++;
  return 0;
}

bool cras_apm_reverse_is_aec_use_case(struct cras_iodev* echo_ref) {
  return cras_apm_reverse_is_aec_use_case_ret;
}
void cras_apm_reverse_deinit() {}

bool cras_iodev_support_rtc_proc_on_dsp(const struct cras_iodev* iodev,
                                        enum RTC_PROC_ON_DSP rtc_proc) {
  return false;
}

int cras_main_message_send(struct cras_main_message* msg) {
  cras_main_message_send_called++;
  sent_apm_message_vector.push_back((struct cras_stream_apm_message*)msg);
  return 0;
}

enum CrasProcessorEffect cras_processor_get_effect(bool nc_provided_by_ap) {
  return NoEffects;
}

}  // extern "C"
}  // namespace
