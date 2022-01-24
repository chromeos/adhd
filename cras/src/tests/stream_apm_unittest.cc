// Copyright 2018 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <gtest/gtest.h>
#include <stdio.h>

extern "C" {
#include "audio_thread.h"
#include "cras_apm_reverse.h"
#include "cras_audio_area.h"
#include "cras_iodev.h"
#include "cras_iodev_list.h"
#include "cras_stream_apm.h"
#include "cras_types.h"
#include "float_buffer.h"
#include "webrtc_apm.h"
}

#define FILENAME_TEMPLATE "ApmTest.XXXXXX"

namespace {

static struct cras_iodev* idev = reinterpret_cast<struct cras_iodev*>(0x345);
static struct cras_iodev* idev2 = reinterpret_cast<cras_iodev*>(0x678);
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
static thread_callback thread_cb;
static void* cb_data;
static output_devices_changed_t output_devices_changed_callback = NULL;

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
  for (i = 0; i < CRAS_CH_MAX; i++)
    fmt->channel_layout[i] = -1;
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

  cras_stream_apm_init("");
  stream = cras_stream_apm_create(APM_ECHO_CANCELLATION);
  EXPECT_NE((void*)NULL, stream);

  for (int i = 0; i < num_test_casts; i++) {
    fmt.num_channels = test_num_channels[i];
    init_channel_layout(&fmt);
    for (ch = 0; ch < CRAS_CH_MAX; ch++)
      fmt.channel_layout[ch] = test_layouts[i][ch];

    /* Input dev is of aec use case. */
    apm = cras_stream_apm_add(stream, idev, &fmt, 1);
    EXPECT_NE((void*)NULL, apm);

    /* Assert that the post-processing format never has an unset
     * first channel in the layout. */
    bool first_channel_found_in_layout = 0;
    val = cras_stream_apm_get_format(apm);
    for (ch = 0; ch < CRAS_CH_MAX; ch++)
      if (0 == val->channel_layout[ch])
        first_channel_found_in_layout = 1;

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

  stream = cras_stream_apm_create(APM_ECHO_CANCELLATION);
  EXPECT_NE((void*)NULL, stream);

  /* Input dev is of aec use case. */
  EXPECT_NE((void*)NULL, cras_stream_apm_add(stream, idev, &fmt, 1));
  EXPECT_NE((void*)NULL, webrtc_apm_create_aec_ini_val);
  EXPECT_NE((void*)NULL, webrtc_apm_create_apm_ini_val);
  EXPECT_EQ((void*)NULL, cras_stream_apm_get_active(stream, idev));
  EXPECT_EQ(0, cras_apm_reverse_state_update_called);

  cras_stream_apm_start(stream, idev);
  EXPECT_NE((void*)NULL, cras_stream_apm_get_active(stream, idev));
  EXPECT_EQ((void*)NULL, cras_stream_apm_get_active(stream, idev2));
  EXPECT_EQ(1, cras_apm_reverse_state_update_called);

  /* Input dev is not of aec use case. */
  EXPECT_NE((void*)NULL, cras_stream_apm_add(stream, idev2, &fmt, 0));
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

  dir = prepare_tempdir();
  cras_stream_apm_init(dir);

  stream = cras_stream_apm_create(APM_ECHO_CANCELLATION);
  EXPECT_NE((void*)NULL, stream);

  /* Output device is of aec use case. */
  cras_apm_reverse_is_aec_use_case_ret = 1;
  EXPECT_NE((void*)NULL, cras_stream_apm_add(stream, idev, &fmt, 1));
  EXPECT_NE((void*)NULL, webrtc_apm_create_aec_ini_val);
  EXPECT_NE((void*)NULL, webrtc_apm_create_apm_ini_val);
  cras_stream_apm_remove(stream, idev);

  /* Output device is not of aec use case. */
  cras_apm_reverse_is_aec_use_case_ret = 0;
  EXPECT_NE((void*)NULL, cras_stream_apm_add(stream, idev, &fmt, 1));
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

  cras_stream_apm_init("");

  stream = cras_stream_apm_create(APM_ECHO_CANCELLATION);
  EXPECT_NE((void*)NULL, stream);

  apm = cras_stream_apm_add(stream, idev, &fmt, 1);

  buf = float_buffer_create(500, 2);
  float_buffer_written(buf, 300);
  webrtc_apm_process_stream_f_called = 0;
  cras_stream_apm_process(apm, buf, 0);
  EXPECT_EQ(0, webrtc_apm_process_stream_f_called);

  area = cras_stream_apm_get_processed(apm);
  EXPECT_EQ(0, area->frames);

  float_buffer_reset(buf);
  float_buffer_written(buf, 200);
  cras_stream_apm_process(apm, buf, 0);
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
  cras_stream_apm_process(apm, buf, 0);
  EXPECT_EQ(1, webrtc_apm_process_stream_f_called);

  /* Put another 280 processed frames, so it's now ready for webrtc_apm
   * to process another chunk of 480 frames (10ms) data.
   */
  cras_stream_apm_put_processed(apm, 280);
  cras_stream_apm_process(apm, buf, 0);
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

  cras_stream_apm_init("");

  webrtc_apm_create_called = 0;
  stream = cras_stream_apm_create(APM_ECHO_CANCELLATION);
  EXPECT_NE((void*)NULL, stream);

  apm1 = cras_stream_apm_add(stream, idev, &fmt, 1);
  EXPECT_EQ(1, webrtc_apm_create_called);
  EXPECT_NE((void*)NULL, apm1);

  apm2 = cras_stream_apm_add(stream, idev, &fmt, 1);
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
int cras_apm_reverse_init(process_reverse_t process_cb,
                          process_reverse_needed_t process_needed_cb,
                          output_devices_changed_t output_devices_changed_cb) {
  output_devices_changed_callback = output_devices_changed_cb;
  return 0;
}

void cras_apm_reverse_state_update() {
  cras_apm_reverse_state_update_called++;
}

bool cras_apm_reverse_is_aec_use_case() {
  return cras_apm_reverse_is_aec_use_case_ret;
}
void cras_apm_reverse_deinit() {}

}  // extern "C"
}  // namespace

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}