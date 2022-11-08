// Copyright 2017 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <gtest/gtest.h>
#include <memory>
#include <stdint.h>
#include <stdio.h>
#include <time.h>
#include <unordered_map>

extern "C" {
#include "cras/src/server/cras_iodev.h"    // stubbed
#include "cras/src/server/cras_rstream.h"  // stubbed
#include "cras/src/server/dev_io.h"        // tested
#include "cras/src/server/dev_stream.h"    // stubbed
#include "cras_shm.h"
#include "cras_types.h"
#include "third_party/utlist/utlist.h"

struct audio_thread_event_log* atlog;
}

#include "cras/src/server/input_data.h"
#include "cras/src/tests/dev_io_stubs.h"
#include "cras/src/tests/iodev_stub.h"
#include "cras/src/tests/metrics_stub.h"
#include "cras/src/tests/rstream_stub.h"

static float dev_stream_capture_software_gain_scaler_val;
static float dev_stream_capture_ui_gain_scaler_val;
static struct input_data_gain input_data_get_software_gain_scaler_ret;
static unsigned int dev_stream_capture_avail_ret = 480;
static bool cras_system_get_force_respect_ui_gains_enabled_ret = false;
static uint64_t cras_stream_apm_get_effects_ret = 0;
static int cras_audio_thread_event_severe_underrun_called;
struct set_dev_rate_data {
  unsigned int dev_rate;
  double dev_rate_ratio;
  double main_rate_ratio;
  int coarse_rate_adjust;
};
std::unordered_map<struct dev_stream*, set_dev_rate_data> set_dev_rate_map;

namespace {

class DevIoSuite : public testing::Test {
 protected:
  virtual void SetUp() {
    atlog = static_cast<audio_thread_event_log*>(calloc(1, sizeof(*atlog)));
    iodev_stub_reset();
    rstream_stub_reset();
    fill_audio_format(&format, 48000);
    set_dev_rate_map.clear();
    stream = create_stream(1, 1, CRAS_STREAM_INPUT, cb_threshold, &format);
    clock_gettime(CLOCK_MONOTONIC_RAW, &ts);
    cras_audio_thread_event_severe_underrun_called = 0;
  }

  virtual void TearDown() { free(atlog); }

  size_t cb_threshold = 480;
  cras_audio_format format;
  StreamPtr stream;
  struct timespec ts;
};

TEST_F(DevIoSuite, SendCapturedFails) {
  // rstream's next callback is now and there is enough data to fill.
  struct timespec start;
  clock_gettime(CLOCK_MONOTONIC_RAW, &start);
  stream->rstream->next_cb_ts = start;
  AddFakeDataToStream(stream.get(), 480);

  struct open_dev* dev_list = NULL;
  DevicePtr dev = create_device(CRAS_STREAM_INPUT, cb_threshold, &format,
                                CRAS_NODE_TYPE_MIC);
  DL_APPEND(dev_list, dev->odev.get());
  add_stream_to_dev(dev->dev, stream);

  // Set failure response from frames_queued.
  iodev_stub_frames_queued(dev->dev.get(), -3, start);

  EXPECT_EQ(-3, dev_io_send_captured_samples(dev_list));
}

TEST_F(DevIoSuite, CaptureGain) {
  struct open_dev* dev_list = NULL;
  struct open_dev* odev_list = NULL;
  DevicePtr dev = create_device(CRAS_STREAM_INPUT, cb_threshold, &format,
                                CRAS_NODE_TYPE_MIC);

  dev->dev->state = CRAS_IODEV_STATE_NORMAL_RUN;
  iodev_stub_frames_queued(dev->dev.get(), 20, ts);
  DL_APPEND(dev_list, dev->odev.get());
  add_stream_to_dev(dev->dev, stream);

  input_data_get_software_gain_scaler_ret = {.preprocessing_scalar = 1.0f,
                                             .postprocessing_scalar = 1.0f};
  dev_io_capture(&dev_list, &odev_list);
  EXPECT_EQ(1.0f, dev_stream_capture_software_gain_scaler_val);

  input_data_get_software_gain_scaler_ret = {.preprocessing_scalar = 1.0f,
                                             .postprocessing_scalar = 0.5f};
  dev_io_capture(&dev_list, &odev_list);
  EXPECT_EQ(0.5f, dev_stream_capture_software_gain_scaler_val);
}

TEST_F(DevIoSuite, CaptureGainIgnoreUiGainsWhenIgnoreUiGainsSet) {
  struct open_dev* dev_list = NULL;
  struct open_dev* odev_list = NULL;
  DevicePtr dev = create_device(CRAS_STREAM_INPUT, cb_threshold, &format,
                                CRAS_NODE_TYPE_MIC);

  dev->dev->state = CRAS_IODEV_STATE_NORMAL_RUN;
  dev->node->ui_gain_scaler = 0.3;
  iodev_stub_frames_queued(dev->dev.get(), 20, ts);
  DL_APPEND(dev_list, dev->odev.get());
  add_stream_to_dev(dev->dev, stream);

  input_data_get_software_gain_scaler_ret = {.preprocessing_scalar = 1.0f,
                                             .postprocessing_scalar = 1.0f};
  cras_system_get_force_respect_ui_gains_enabled_ret = false;
  cras_stream_apm_get_effects_ret = IGNORE_UI_GAINS;
  dev_io_capture(&dev_list, &odev_list);
  EXPECT_EQ(1.0f, dev_stream_capture_ui_gain_scaler_val);
}

class DevIoSuiteRespectUiGains
    : public DevIoSuite,
      public testing::WithParamInterface<testing::tuple<bool, uint64_t>> {};

TEST_P(DevIoSuiteRespectUiGains, CaptureGainRespectUiGains) {
  struct open_dev* dev_list = NULL;
  struct open_dev* odev_list = NULL;
  DevicePtr dev = create_device(CRAS_STREAM_INPUT, cb_threshold, &format,
                                CRAS_NODE_TYPE_MIC);

  dev->dev->state = CRAS_IODEV_STATE_NORMAL_RUN;
  dev->node->ui_gain_scaler = 0.3;
  iodev_stub_frames_queued(dev->dev.get(), 20, ts);
  DL_APPEND(dev_list, dev->odev.get());
  add_stream_to_dev(dev->dev, stream);

  input_data_get_software_gain_scaler_ret = {.preprocessing_scalar = 1.0f,
                                             .postprocessing_scalar = 1.0f};
  cras_system_get_force_respect_ui_gains_enabled_ret = std::get<0>(GetParam());
  cras_stream_apm_get_effects_ret = std::get<1>(GetParam());
  dev_io_capture(&dev_list, &odev_list);
  EXPECT_EQ(0.3f, dev_stream_capture_ui_gain_scaler_val);
}

INSTANTIATE_TEST_SUITE_P(
    CaptureGainRespectUiGainsTest,
    DevIoSuiteRespectUiGains,
    ::testing::ValuesIn(
        {std::make_tuple(false, static_cast<uint64_t>(0)),
         std::make_tuple(true, static_cast<uint64_t>(0)),
         std::make_tuple(true, static_cast<uint64_t>(IGNORE_UI_GAINS))}));

/*
 * When input and output devices are on the internal sound card,
 * and their device rates are the same, use the estimated rate
 * on the output device as the estimated rate of input device.
 */
TEST_F(DevIoSuite, CopyOutputEstimatedRate) {
  struct open_dev* idev_list = NULL;
  struct open_dev* odev_list = NULL;
  DevicePtr out_dev = create_device(CRAS_STREAM_OUTPUT, cb_threshold, &format,
                                    CRAS_NODE_TYPE_INTERNAL_SPEAKER);
  DevicePtr in_dev = create_device(CRAS_STREAM_INPUT, cb_threshold, &format,
                                   CRAS_NODE_TYPE_MIC);

  in_dev->dev->state = CRAS_IODEV_STATE_NORMAL_RUN;
  iodev_stub_frames_queued(in_dev->dev.get(), 20, ts);
  DL_APPEND(idev_list, in_dev->odev.get());
  add_stream_to_dev(in_dev->dev, stream);
  DL_APPEND(odev_list, out_dev->odev.get());
  iodev_stub_on_internal_card(out_dev->dev->active_node, 1);
  iodev_stub_on_internal_card(in_dev->dev->active_node, 1);

  iodev_stub_est_rate_ratio(in_dev->dev.get(), 0.8f);
  iodev_stub_est_rate_ratio(out_dev->dev.get(), 1.2f);

  dev_io_capture(&idev_list, &odev_list);

  EXPECT_FLOAT_EQ(1.2f, set_dev_rate_map[stream->dstream.get()].dev_rate_ratio);
}

/*
 * When input and output devices are not both on the internal sound card,
 * estimated rates are independent.
 */
TEST_F(DevIoSuite, InputOutputIndependentEstimatedRate) {
  struct open_dev* idev_list = NULL;
  struct open_dev* odev_list = NULL;
  DevicePtr out_dev = create_device(CRAS_STREAM_OUTPUT, cb_threshold, &format,
                                    CRAS_NODE_TYPE_INTERNAL_SPEAKER);
  DevicePtr in_dev = create_device(CRAS_STREAM_INPUT, cb_threshold, &format,
                                   CRAS_NODE_TYPE_USB);

  in_dev->dev->state = CRAS_IODEV_STATE_NORMAL_RUN;
  iodev_stub_frames_queued(in_dev->dev.get(), 20, ts);
  DL_APPEND(idev_list, in_dev->odev.get());
  add_stream_to_dev(in_dev->dev, stream);
  DL_APPEND(odev_list, out_dev->odev.get());
  iodev_stub_on_internal_card(out_dev->dev->active_node, 1);
  iodev_stub_on_internal_card(in_dev->dev->active_node, 0);

  iodev_stub_est_rate_ratio(in_dev->dev.get(), 0.8f);
  iodev_stub_est_rate_ratio(out_dev->dev.get(), 1.2f);
  iodev_stub_update_rate(in_dev->dev.get(), 1);

  dev_io_capture(&idev_list, &odev_list);

  EXPECT_FLOAT_EQ(0.8f, set_dev_rate_map[stream->dstream.get()].dev_rate_ratio);
}

/*
 * If any hw_level is larger than 1.5 * largest_cb_level and
 * DROP_FRAMES_THRESHOLD_MS, reset all input devices.
 */
TEST_F(DevIoSuite, SendCapturedNeedToResetDevices) {
  struct timespec start;
  struct timespec drop_time;
  struct open_dev* dev_list = NULL;
  bool rc;

  clock_gettime(CLOCK_MONOTONIC_RAW, &start);
  AddFakeDataToStream(stream.get(), 0);

  DevicePtr dev1 =
      create_device(CRAS_STREAM_INPUT, 1000, &format, CRAS_NODE_TYPE_MIC);
  DevicePtr dev2 =
      create_device(CRAS_STREAM_INPUT, 10000, &format, CRAS_NODE_TYPE_MIC);
  DL_APPEND(dev_list, dev1->odev.get());
  DL_APPEND(dev_list, dev2->odev.get());
  add_stream_to_dev(dev1->dev, stream);
  add_stream_to_dev(dev2->dev, stream);

  iodev_stub_frames_queued(dev1->dev.get(), 2880, start);
  iodev_stub_frames_queued(dev2->dev.get(), 4800, start);
  EXPECT_EQ(0, dev_io_send_captured_samples(dev_list));

  /*
   * Should drop frames to one min_cb_level, which is MIN(2880, 4800) - 480 =
   * 2400 (50ms).
   */
  rc = iodev_stub_get_drop_time(dev1->dev.get(), &drop_time);
  EXPECT_EQ(true, rc);
  EXPECT_EQ(0, drop_time.tv_sec);
  EXPECT_EQ(50000000, drop_time.tv_nsec);

  rc = iodev_stub_get_drop_time(dev2->dev.get(), &drop_time);
  EXPECT_EQ(true, rc);
  EXPECT_EQ(0, drop_time.tv_sec);
  EXPECT_EQ(50000000, drop_time.tv_nsec);
}

/*
 * If any hw_level is larger than 0.5 * buffer_size and
 * DROP_FRAMES_THRESHOLD_MS, reset all input devices.
 */

TEST_F(DevIoSuite, SendCapturedNeedToResetDevices2) {
  struct timespec start;
  struct timespec drop_time;
  struct open_dev* dev_list = NULL;
  bool rc;

  stream = create_stream(1, 1, CRAS_STREAM_INPUT, 2000, &format);

  clock_gettime(CLOCK_MONOTONIC_RAW, &start);
  AddFakeDataToStream(stream.get(), 0);

  DevicePtr dev1 =
      create_device(CRAS_STREAM_INPUT, 2048, &format, CRAS_NODE_TYPE_MIC);
  DevicePtr dev2 =
      create_device(CRAS_STREAM_INPUT, 10000, &format, CRAS_NODE_TYPE_MIC);
  DL_APPEND(dev_list, dev1->odev.get());
  DL_APPEND(dev_list, dev2->odev.get());
  add_stream_to_dev(dev1->dev, stream);
  add_stream_to_dev(dev2->dev, stream);

  iodev_stub_frames_queued(dev1->dev.get(), 2480, start);
  iodev_stub_frames_queued(dev2->dev.get(), 2480, start);
  EXPECT_EQ(0, dev_io_send_captured_samples(dev_list));

  /*
   * Should drop frames to one min_cb_level, which is 2480 - 2000 = 480 (10ms).
   */
  rc = iodev_stub_get_drop_time(dev1->dev.get(), &drop_time);
  EXPECT_EQ(true, rc);
  EXPECT_EQ(0, drop_time.tv_sec);
  EXPECT_EQ(10000000, drop_time.tv_nsec);

  rc = iodev_stub_get_drop_time(dev2->dev.get(), &drop_time);
  EXPECT_EQ(true, rc);
  EXPECT_EQ(0, drop_time.tv_sec);
  EXPECT_EQ(10000000, drop_time.tv_nsec);
}

/*
 * If the hw_level is larger than 1.5 * largest_cb_level but less than
 * DROP_FRAMES_THRESHOLD_MS, do nothing.
 */
TEST_F(DevIoSuite, SendCapturedLevelLessThanThreshold) {
  struct timespec start;
  struct timespec drop_time;
  struct open_dev* dev_list = NULL;
  bool rc;

  clock_gettime(CLOCK_MONOTONIC_RAW, &start);
  AddFakeDataToStream(stream.get(), 0);

  DevicePtr dev =
      create_device(CRAS_STREAM_INPUT, 480, &format, CRAS_NODE_TYPE_MIC);
  DL_APPEND(dev_list, dev->odev.get());
  add_stream_to_dev(dev->dev, stream);

  iodev_stub_frames_queued(dev->dev.get(), 2048, start);
  EXPECT_EQ(0, dev_io_send_captured_samples(dev_list));

  rc = iodev_stub_get_drop_time(dev->dev.get(), &drop_time);
  EXPECT_EQ(false, rc);
}

/*
 * If all hw_level is less than 1.5 * largest_cb_level and 0.5 * buffer_size,
 * do nothing.
 */
TEST_F(DevIoSuite, SendCapturedNoNeedToResetDevices) {
  struct timespec start;
  struct timespec drop_time;
  struct open_dev* dev_list = NULL;
  bool rc;

  clock_gettime(CLOCK_MONOTONIC_RAW, &start);
  AddFakeDataToStream(stream.get(), 0);

  DevicePtr dev1 =
      create_device(CRAS_STREAM_INPUT, 1000, &format, CRAS_NODE_TYPE_MIC);
  DevicePtr dev2 =
      create_device(CRAS_STREAM_INPUT, 10000, &format, CRAS_NODE_TYPE_MIC);
  DL_APPEND(dev_list, dev1->odev.get());
  DL_APPEND(dev_list, dev2->odev.get());
  add_stream_to_dev(dev1->dev, stream);
  add_stream_to_dev(dev2->dev, stream);

  iodev_stub_frames_queued(dev1->dev.get(), 400, start);
  iodev_stub_frames_queued(dev2->dev.get(), 400, start);
  EXPECT_EQ(0, dev_io_send_captured_samples(dev_list));

  rc = iodev_stub_get_drop_time(dev1->dev.get(), &drop_time);
  EXPECT_EQ(false, rc);

  rc = iodev_stub_get_drop_time(dev2->dev.get(), &drop_time);
  EXPECT_EQ(false, rc);
}

/*
 * On loopback and hotword devices, if any hw_level is larger than
 *  1.5 * largest_cb_level and DROP_FRAMES_THRESHOLD_MS, do nothing.
 */
TEST_F(DevIoSuite, SendCapturedNoNeedToDrop) {
  struct timespec start;
  struct timespec drop_time;
  struct open_dev* dev_list = NULL;
  bool rc;

  clock_gettime(CLOCK_MONOTONIC_RAW, &start);
  AddFakeDataToStream(stream.get(), 0);

  DevicePtr dev1 =
      create_device(CRAS_STREAM_INPUT, 480, &format, CRAS_NODE_TYPE_HOTWORD);
  DevicePtr dev2 = create_device(CRAS_STREAM_INPUT, 480, &format,
                                 CRAS_NODE_TYPE_POST_MIX_PRE_DSP);
  DevicePtr dev3 =
      create_device(CRAS_STREAM_INPUT, 480, &format, CRAS_NODE_TYPE_POST_DSP);

  DL_APPEND(dev_list, dev1->odev.get());
  DL_APPEND(dev_list, dev2->odev.get());
  DL_APPEND(dev_list, dev3->odev.get());

  add_stream_to_dev(dev1->dev, stream);
  add_stream_to_dev(dev2->dev, stream);
  add_stream_to_dev(dev3->dev, stream);

  iodev_stub_frames_queued(dev1->dev.get(), 4800, start);
  iodev_stub_frames_queued(dev2->dev.get(), 4800, start);
  iodev_stub_frames_queued(dev2->dev.get(), 4800, start);

  EXPECT_EQ(0, dev_io_send_captured_samples(dev_list));

  rc = iodev_stub_get_drop_time(dev1->dev.get(), &drop_time);
  EXPECT_EQ(false, rc);

  rc = iodev_stub_get_drop_time(dev2->dev.get(), &drop_time);
  EXPECT_EQ(false, rc);

  rc = iodev_stub_get_drop_time(dev3->dev.get(), &drop_time);
  EXPECT_EQ(false, rc);
}

TEST_F(DevIoSuite, PlaybackWriteSevereUnderrun) {
  struct open_dev* dev_list = nullptr;
  DevicePtr dev1 = create_device(CRAS_STREAM_OUTPUT, cb_threshold, &format,
                                 CRAS_NODE_TYPE_INTERNAL_SPEAKER);
  dev1->dev->state = CRAS_IODEV_STATE_NORMAL_RUN;
  iodev_stub_frames_queued(dev1->dev.get(), -EPIPE, ts);

  /* releasing the odev as it will be freed by handle_dev_err in
   * dev_io_playback_write */
  DL_APPEND(dev_list, dev1->odev.release());

  // verify that our test setup returns -EPIPE
  ASSERT_EQ(write_output_samples(nullptr, dev_list, nullptr), -EPIPE);

  dev_io_playback_write(&dev_list, nullptr);

  EXPECT_EQ(dev_list, nullptr);
  EXPECT_EQ(cras_audio_thread_event_severe_underrun_called, 1);
}

// Stubs
extern "C" {

int input_data_get_for_stream(struct input_data* data,
                              struct cras_rstream* stream,
                              struct buffer_share* offsets,
                              float preprocessing_gain_scalar,
                              struct cras_audio_area** area,
                              unsigned int* offset) {
  return 0;
}

int input_data_put_for_stream(struct input_data* data,
                              struct cras_rstream* stream,
                              struct buffer_share* offsets,
                              unsigned int frames) {
  return 0;
}

struct input_data_gain input_data_get_software_gain_scaler(
    struct input_data* data,
    float ui_gain_scaler,
    float idev_sw_gain_scaler,
    struct cras_rstream* stream) {
  dev_stream_capture_ui_gain_scaler_val = ui_gain_scaler;
  return input_data_get_software_gain_scaler_ret;
}

int cras_audio_thread_event_drop_samples() {
  return 0;
}

int cras_audio_thread_event_severe_underrun() {
  cras_audio_thread_event_severe_underrun_called++;
  return 0;
}

int dev_stream_attached_devs(const struct dev_stream* dev_stream) {
  return 0;
}
void dev_stream_update_frames(const struct dev_stream* dev_stream) {}
int dev_stream_playback_frames(const struct dev_stream* dev_stream) {
  return 0;
}
int dev_stream_is_pending_reply(const struct dev_stream* dev_stream) {
  return 0;
}
int dev_stream_mix(struct dev_stream* dev_stream,
                   const struct cras_audio_format* fmt,
                   uint8_t* dst,
                   unsigned int num_to_write) {
  return 0;
}
void dev_stream_set_dev_rate(struct dev_stream* dev_stream,
                             unsigned int dev_rate,
                             double dev_rate_ratio,
                             double main_rate_ratio,
                             int coarse_rate_adjust) {
  set_dev_rate_data new_data;
  new_data.dev_rate = dev_rate;
  new_data.dev_rate_ratio = dev_rate_ratio;
  new_data.main_rate_ratio = main_rate_ratio;
  new_data.coarse_rate_adjust = coarse_rate_adjust;

  set_dev_rate_map[dev_stream] = new_data;
}
int dev_stream_capture_update_rstream(struct dev_stream* dev_stream) {
  return 0;
}
int dev_stream_wake_time(struct dev_stream* dev_stream,
                         unsigned int curr_level,
                         struct timespec* level_tstamp,
                         unsigned int cap_limit,
                         int is_cap_limit_stream,
                         struct timespec* wake_time_out) {
  return 0;
}
int dev_stream_flush_old_audio_messages(struct dev_stream* dev_stream) {
  return 0;
}
void dev_stream_set_delay(const struct dev_stream* dev_stream,
                          unsigned int delay_frames) {}
unsigned int dev_stream_capture(struct dev_stream* dev_stream,
                                const struct cras_audio_area* area,
                                unsigned int area_offset,
                                float software_gain_scaler) {
  dev_stream_capture_software_gain_scaler_val = software_gain_scaler;
  return 0;
}
void dev_stream_update_next_wake_time(struct dev_stream* dev_stream) {}
int dev_stream_request_playback_samples(struct dev_stream* dev_stream,
                                        const struct timespec* now) {
  return 0;
}
int dev_stream_playback_update_rstream(struct dev_stream* dev_stream) {
  return 0;
}
void dev_stream_destroy(struct dev_stream* dev_stream) {}
unsigned int dev_stream_capture_avail(const struct dev_stream* dev_stream) {
  return dev_stream_capture_avail_ret;
}
struct dev_stream* dev_stream_create(struct cras_rstream* stream,
                                     unsigned int dev_id,
                                     const struct cras_audio_format* dev_fmt,
                                     struct cras_iodev* iodev,
                                     struct timespec* cb_ts,
                                     const struct timespec* sleep_interval_ts) {
  return 0;
}
int cras_device_monitor_error_close(unsigned int dev_idx) {
  return 0;
}
int cras_system_get_capture_mute() {
  return 0;
}

bool cras_system_get_force_respect_ui_gains_enabled() {
  return cras_system_get_force_respect_ui_gains_enabled_ret;
}

uint64_t cras_stream_apm_get_effects(struct cras_stream_apm* stream) {
  return cras_stream_apm_get_effects_ret;
}

}  // extern "C"

}  //  namespace
