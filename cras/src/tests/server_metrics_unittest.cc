// Copyright 2016 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <gtest/gtest.h>
#include <map>
#include <stdio.h>
#include <string>
#include <tuple>
#include <vector>

#include "cras/server/main_message.h"
#include "cras/src/server/cras_rstream.h"
#include "cras/src/server/cras_server_metrics.h"

extern "C" {
#include "cras/src/server/cras_server_metrics.c"
}

static enum CRAS_MAIN_MESSAGE_TYPE type_set;
static struct timespec clock_gettime_retspec;
static std::vector<std::tuple<std::string, int, int, int, int>>
    cras_metrics_log_histogram_called_args;
static std::vector<std::tuple<std::string, int>>
    cras_metrics_log_sparse_histogram_called_args;
static int cras_system_state_in_main_thread_ret = 0;
std::vector<struct cras_server_metrics_message> sent_msgs;

void ResetStubData() {
  type_set = (enum CRAS_MAIN_MESSAGE_TYPE)0;
  cras_metrics_log_histogram_called_args.clear();
  cras_metrics_log_sparse_histogram_called_args.clear();
  cras_system_state_in_main_thread_ret = 0;
  sent_msgs.clear();
}

namespace {

TEST(ServerMetricsTestSuite, Init) {
  ResetStubData();

  cras_server_metrics_init();

  EXPECT_EQ(type_set, CRAS_MAIN_METRICS);
}

TEST(ServerMetricsTestSuite, SetMetricsDeviceRuntime) {
  ResetStubData();
  struct cras_iodev iodev = {};
  struct cras_ionode active_node = {};

  clock_gettime_retspec.tv_sec = 200;
  clock_gettime_retspec.tv_nsec = 0;
  iodev.info.idx = MAX_SPECIAL_DEVICE_IDX;
  iodev.open_ts.tv_sec = 100;
  iodev.open_ts.tv_nsec = 0;
  iodev.direction = CRAS_STREAM_INPUT;
  iodev.active_node = &active_node;
  active_node.type = CRAS_NODE_TYPE_USB;

  cras_server_metrics_device_runtime(&iodev);

  EXPECT_EQ(sent_msgs.size(), 1);
  EXPECT_EQ(sent_msgs[0].header.type, CRAS_MAIN_METRICS);
  EXPECT_EQ(sent_msgs[0].header.length,
            sizeof(struct cras_server_metrics_message));
  EXPECT_EQ(sent_msgs[0].metrics_type, DEVICE_RUNTIME);
  EXPECT_EQ(sent_msgs[0].data.device_data.type, CRAS_METRICS_DEVICE_USB);
  EXPECT_EQ(sent_msgs[0].data.device_data.direction, CRAS_STREAM_INPUT);
  EXPECT_EQ(sent_msgs[0].data.device_data.runtime.tv_sec, 100);
  EXPECT_EQ(sent_msgs[0].data.device_data.use_case, CRAS_USE_CASE_HIFI);

  sent_msgs.clear();

  clock_gettime_retspec.tv_sec = 300;
  clock_gettime_retspec.tv_nsec = 0;
  iodev.open_ts.tv_sec = 100;
  iodev.open_ts.tv_nsec = 0;
  iodev.direction = CRAS_STREAM_OUTPUT;
  iodev.get_use_case = [](const struct cras_iodev* iodev) {
    return CRAS_USE_CASE_LOW_LATENCY;
  };
  iodev.active_node = &active_node;
  active_node.type = CRAS_NODE_TYPE_HEADPHONE;

  cras_server_metrics_device_runtime(&iodev);

  EXPECT_EQ(sent_msgs.size(), 1);
  EXPECT_EQ(sent_msgs[0].header.type, CRAS_MAIN_METRICS);
  EXPECT_EQ(sent_msgs[0].header.length,
            sizeof(struct cras_server_metrics_message));
  EXPECT_EQ(sent_msgs[0].metrics_type, DEVICE_RUNTIME);
  EXPECT_EQ(sent_msgs[0].data.device_data.type, CRAS_METRICS_DEVICE_HEADPHONE);
  EXPECT_EQ(sent_msgs[0].data.device_data.direction, CRAS_STREAM_OUTPUT);
  EXPECT_EQ(sent_msgs[0].data.device_data.runtime.tv_sec, 200);
  EXPECT_EQ(sent_msgs[0].data.device_data.use_case, CRAS_USE_CASE_LOW_LATENCY);

  std::map<uint32_t, enum CRAS_METRICS_DEVICE_TYPE> btflags_to_device = {
      {CRAS_BT_FLAG_A2DP, CRAS_METRICS_DEVICE_A2DP},
      {CRAS_BT_FLAG_HFP, CRAS_METRICS_DEVICE_HFP},
  };

  for (const auto& btflags_device : btflags_to_device) {
    uint32_t btflags = btflags_device.first;
    enum CRAS_METRICS_DEVICE_TYPE device_type = btflags_device.second;

    sent_msgs.clear();

    clock_gettime_retspec.tv_sec = 300;
    clock_gettime_retspec.tv_nsec = 0;
    iodev.open_ts.tv_sec = 100;
    iodev.open_ts.tv_nsec = 0;
    iodev.direction = CRAS_STREAM_OUTPUT;
    iodev.active_node = &active_node;
    active_node.type = CRAS_NODE_TYPE_BLUETOOTH;
    active_node.btflags = btflags;

    cras_server_metrics_device_runtime(&iodev);

    EXPECT_EQ(sent_msgs.size(), 1);
    EXPECT_EQ(sent_msgs[0].header.type, CRAS_MAIN_METRICS);
    EXPECT_EQ(sent_msgs[0].header.length,
              sizeof(struct cras_server_metrics_message));
    EXPECT_EQ(sent_msgs[0].metrics_type, DEVICE_RUNTIME);
    EXPECT_EQ(sent_msgs[0].data.device_data.type, device_type);
    EXPECT_EQ(sent_msgs[0].data.device_data.direction, CRAS_STREAM_OUTPUT);
    EXPECT_EQ(sent_msgs[0].data.device_data.runtime.tv_sec, 200);
  }
}

TEST(ServerMetricsTestSuite, SetMetricsHighestDeviceDelay) {
  ResetStubData();
  unsigned int hw_level = 1000;
  unsigned int largest_cb_level = 500;

  cras_server_metrics_highest_device_delay(hw_level, largest_cb_level,
                                           CRAS_STREAM_INPUT);

  EXPECT_EQ(sent_msgs.size(), 1);
  EXPECT_EQ(sent_msgs[0].header.type, CRAS_MAIN_METRICS);
  EXPECT_EQ(sent_msgs[0].header.length,
            sizeof(struct cras_server_metrics_message));
  EXPECT_EQ(sent_msgs[0].metrics_type, HIGHEST_DEVICE_DELAY_INPUT);
  EXPECT_EQ(sent_msgs[0].data.value, 2000);

  sent_msgs.clear();

  cras_server_metrics_highest_device_delay(hw_level, largest_cb_level,
                                           CRAS_STREAM_OUTPUT);

  EXPECT_EQ(sent_msgs.size(), 1);
  EXPECT_EQ(sent_msgs[0].header.type, CRAS_MAIN_METRICS);
  EXPECT_EQ(sent_msgs[0].header.length,
            sizeof(struct cras_server_metrics_message));
  EXPECT_EQ(sent_msgs[0].metrics_type, HIGHEST_DEVICE_DELAY_OUTPUT);
  EXPECT_EQ(sent_msgs[0].data.value, 2000);
}

TEST(ServerMetricsTestSuite, SetMetricHighestHardwareLevel) {
  ResetStubData();
  unsigned int hw_level = 1000;

  cras_server_metrics_highest_hw_level(hw_level, CRAS_STREAM_INPUT);

  EXPECT_EQ(sent_msgs.size(), 1);
  EXPECT_EQ(sent_msgs[0].header.type, CRAS_MAIN_METRICS);
  EXPECT_EQ(sent_msgs[0].header.length,
            sizeof(struct cras_server_metrics_message));
  EXPECT_EQ(sent_msgs[0].metrics_type, HIGHEST_INPUT_HW_LEVEL);
  EXPECT_EQ(sent_msgs[0].data.value, hw_level);

  sent_msgs.clear();

  cras_server_metrics_highest_hw_level(hw_level, CRAS_STREAM_OUTPUT);

  EXPECT_EQ(sent_msgs.size(), 1);
  EXPECT_EQ(sent_msgs[0].header.type, CRAS_MAIN_METRICS);
  EXPECT_EQ(sent_msgs[0].header.length,
            sizeof(struct cras_server_metrics_message));
  EXPECT_EQ(sent_msgs[0].metrics_type, HIGHEST_OUTPUT_HW_LEVEL);
  EXPECT_EQ(sent_msgs[0].data.value, hw_level);
}

TEST(ServerMetricsTestSuite, SetMetricsNumUnderruns) {
  ResetStubData();
  struct cras_iodev iodev = {};
  struct cras_ionode active_node;

  iodev.num_underruns = 10;
  iodev.active_node = &active_node;
  iodev.info.idx = MAX_SPECIAL_DEVICE_IDX;
  active_node.type = CRAS_NODE_TYPE_INTERNAL_SPEAKER;

  cras_server_metrics_num_underruns(&iodev);

  EXPECT_EQ(sent_msgs.size(), 1);
  EXPECT_EQ(sent_msgs[0].header.type, CRAS_MAIN_METRICS);
  EXPECT_EQ(sent_msgs[0].header.length,
            sizeof(struct cras_server_metrics_message));
  EXPECT_EQ(sent_msgs[0].metrics_type, NUM_UNDERRUNS);
  EXPECT_EQ(sent_msgs[0].data.device_data.value, iodev.num_underruns);
  EXPECT_EQ(sent_msgs[0].data.device_data.type,
            CRAS_METRICS_DEVICE_INTERNAL_SPEAKER);
}

TEST(ServerMetricsTestSuite, SetMetricsMissedCallbackEventInputStream) {
  ResetStubData();
  struct cras_rstream stream;
  struct timespec diff_ts;

  stream.flags = 0;
  stream.start_ts.tv_sec = 0;
  stream.start_ts.tv_nsec = 0;
  stream.num_missed_cb = 0;
  stream.direction = CRAS_STREAM_INPUT;

  clock_gettime_retspec.tv_sec = 100;
  clock_gettime_retspec.tv_nsec = 0;
  cras_server_metrics_missed_cb_event(&stream);

  subtract_timespecs(&clock_gettime_retspec, &stream.start_ts, &diff_ts);
  EXPECT_EQ(sent_msgs.size(), 1);
  EXPECT_EQ(sent_msgs[0].header.type, CRAS_MAIN_METRICS);
  EXPECT_EQ(sent_msgs[0].header.length,
            sizeof(struct cras_server_metrics_message));
  EXPECT_EQ(sent_msgs[0].metrics_type, MISSED_CB_FIRST_TIME_INPUT);
  EXPECT_EQ(sent_msgs[0].data.value, diff_ts.tv_sec);
  EXPECT_EQ(stream.num_missed_cb, 1);
  EXPECT_EQ(stream.first_missed_cb_ts.tv_sec, clock_gettime_retspec.tv_sec);
  EXPECT_EQ(stream.first_missed_cb_ts.tv_nsec, clock_gettime_retspec.tv_nsec);

  clock_gettime_retspec.tv_sec = 200;
  clock_gettime_retspec.tv_nsec = 0;
  cras_server_metrics_missed_cb_event(&stream);

  subtract_timespecs(&clock_gettime_retspec, &stream.first_missed_cb_ts,
                     &diff_ts);
  EXPECT_EQ(sent_msgs.size(), 2);
  EXPECT_EQ(sent_msgs[1].header.type, CRAS_MAIN_METRICS);
  EXPECT_EQ(sent_msgs[1].header.length,
            sizeof(struct cras_server_metrics_message));
  EXPECT_EQ(sent_msgs[1].metrics_type, MISSED_CB_SECOND_TIME_INPUT);
  EXPECT_EQ(sent_msgs[1].data.value, diff_ts.tv_sec);
  EXPECT_EQ(stream.num_missed_cb, 2);
}

TEST(ServerMetricsTestSuite, SetMetricsMissedCallbackEventOutputStream) {
  ResetStubData();
  struct cras_rstream stream;
  struct timespec diff_ts;

  stream.flags = 0;
  stream.start_ts.tv_sec = 0;
  stream.start_ts.tv_nsec = 0;
  stream.num_missed_cb = 0;
  stream.direction = CRAS_STREAM_OUTPUT;

  clock_gettime_retspec.tv_sec = 100;
  clock_gettime_retspec.tv_nsec = 0;
  cras_server_metrics_missed_cb_event(&stream);

  subtract_timespecs(&clock_gettime_retspec, &stream.start_ts, &diff_ts);
  EXPECT_EQ(sent_msgs.size(), 1);
  EXPECT_EQ(sent_msgs[0].header.type, CRAS_MAIN_METRICS);
  EXPECT_EQ(sent_msgs[0].header.length,
            sizeof(struct cras_server_metrics_message));
  EXPECT_EQ(sent_msgs[0].metrics_type, MISSED_CB_FIRST_TIME_OUTPUT);
  EXPECT_EQ(sent_msgs[0].data.value, diff_ts.tv_sec);
  EXPECT_EQ(stream.num_missed_cb, 1);
  EXPECT_EQ(stream.first_missed_cb_ts.tv_sec, clock_gettime_retspec.tv_sec);
  EXPECT_EQ(stream.first_missed_cb_ts.tv_nsec, clock_gettime_retspec.tv_nsec);

  clock_gettime_retspec.tv_sec = 200;
  clock_gettime_retspec.tv_nsec = 0;
  cras_server_metrics_missed_cb_event(&stream);

  subtract_timespecs(&clock_gettime_retspec, &stream.first_missed_cb_ts,
                     &diff_ts);
  EXPECT_EQ(sent_msgs.size(), 2);
  EXPECT_EQ(sent_msgs[1].header.type, CRAS_MAIN_METRICS);
  EXPECT_EQ(sent_msgs[1].header.length,
            sizeof(struct cras_server_metrics_message));
  EXPECT_EQ(sent_msgs[1].metrics_type, MISSED_CB_SECOND_TIME_OUTPUT);
  EXPECT_EQ(sent_msgs[1].data.value, diff_ts.tv_sec);
  EXPECT_EQ(stream.num_missed_cb, 2);
}

TEST(ServerMetricsTestSuite, SetMetricsStreamCreate) {
  ResetStubData();
  struct cras_rstream_config config;
  struct cras_audio_format format;

  config.direction = CRAS_STREAM_INPUT;
  config.cb_threshold = 1024;
  config.flags = BULK_AUDIO_OK;
  format.format = SND_PCM_FORMAT_S16_LE;
  format.frame_rate = 48000;
  format.num_channels = 2;
  config.client_type = CRAS_CLIENT_TYPE_TEST;
  config.format = &format;
  cras_server_metrics_stream_create(&config);

  // Log stream config.
  EXPECT_EQ(sent_msgs.size(), 1);
  EXPECT_EQ(sent_msgs[0].header.type, CRAS_MAIN_METRICS);
  EXPECT_EQ(sent_msgs[0].header.length,
            sizeof(struct cras_server_metrics_message));
  EXPECT_EQ(sent_msgs[0].metrics_type, STREAM_CONFIG);
  EXPECT_EQ(sent_msgs[0].data.stream_config.direction, CRAS_STREAM_INPUT);
  EXPECT_EQ(sent_msgs[0].data.stream_config.cb_threshold, 1024);
  EXPECT_EQ(sent_msgs[0].data.stream_config.flags, BULK_AUDIO_OK);
  EXPECT_EQ(sent_msgs[0].data.stream_config.format, SND_PCM_FORMAT_S16_LE);
  EXPECT_EQ(sent_msgs[0].data.stream_config.rate, 48000);
  EXPECT_EQ(sent_msgs[0].data.stream_config.num_channels, 2);
  EXPECT_EQ(sent_msgs[0].data.stream_config.client_type, CRAS_CLIENT_TYPE_TEST);
}

TEST(ServerMetricsTestSuite, SetMetricsStreamDestroy) {
  ResetStubData();
  struct cras_rstream stream;
  struct timespec diff_ts;

  stream.flags = 0;
  stream.start_ts.tv_sec = 0;
  stream.start_ts.tv_nsec = 0;
  clock_gettime_retspec.tv_sec = 1000;
  clock_gettime_retspec.tv_nsec = 0;
  stream.num_missed_cb = 5;
  stream.first_missed_cb_ts.tv_sec = 100;
  stream.first_missed_cb_ts.tv_nsec = 0;
  stream.longest_fetch_interval.tv_sec = 1;
  stream.longest_fetch_interval.tv_nsec = 0;
  stream.sleep_interval_ts.tv_sec = 0;
  stream.sleep_interval_ts.tv_nsec = 5000000;
  stream.num_delayed_fetches = 10;
  stream.shm = static_cast<cras_audio_shm*>(calloc(1, sizeof(cras_audio_shm)));
  stream.shm->header = static_cast<cras_audio_shm_header*>(
      calloc(1, sizeof(cras_audio_shm_header)));
  stream.shm->header->num_overruns = 3;
  stream.shm->header->overrun_frames = 1000;

  stream.direction = CRAS_STREAM_INPUT;
  stream.client_type = CRAS_CLIENT_TYPE_TEST;
  stream.stream_type = CRAS_STREAM_TYPE_DEFAULT;
  cras_server_metrics_stream_destroy(&stream);

  subtract_timespecs(&clock_gettime_retspec, &stream.start_ts, &diff_ts);
  EXPECT_EQ(sent_msgs.size(), 7);

  // Log missed cb frequency.
  EXPECT_EQ(sent_msgs[0].header.type, CRAS_MAIN_METRICS);
  EXPECT_EQ(sent_msgs[0].header.length,
            sizeof(struct cras_server_metrics_message));
  EXPECT_EQ(sent_msgs[0].metrics_type, MISSED_CB_FREQUENCY_INPUT);
  EXPECT_EQ(sent_msgs[0].data.value,
            stream.num_missed_cb * 86400 / diff_ts.tv_sec);

  // Log missed cb frequency after rescheduling.
  subtract_timespecs(&clock_gettime_retspec, &stream.first_missed_cb_ts,
                     &diff_ts);
  EXPECT_EQ(sent_msgs[1].header.type, CRAS_MAIN_METRICS);
  EXPECT_EQ(sent_msgs[1].header.length,
            sizeof(struct cras_server_metrics_message));
  EXPECT_EQ(sent_msgs[1].metrics_type,
            MISSED_CB_FREQUENCY_AFTER_RESCHEDULING_INPUT);
  EXPECT_EQ(sent_msgs[1].data.value,
            (stream.num_missed_cb - 1) * 86400 / diff_ts.tv_sec);

  // Log stream runtime.
  EXPECT_EQ(sent_msgs[2].header.type, CRAS_MAIN_METRICS);
  EXPECT_EQ(sent_msgs[2].header.length,
            sizeof(struct cras_server_metrics_message));
  EXPECT_EQ(sent_msgs[2].metrics_type, STREAM_RUNTIME);
  EXPECT_EQ(sent_msgs[2].data.stream_data.client_type, CRAS_CLIENT_TYPE_TEST);
  EXPECT_EQ(sent_msgs[2].data.stream_data.stream_type,
            CRAS_STREAM_TYPE_DEFAULT);
  EXPECT_EQ(sent_msgs[2].data.stream_data.direction, CRAS_STREAM_INPUT);
  EXPECT_EQ(sent_msgs[2].data.stream_data.runtime.tv_sec, 1000);

  // Log longest fetch delay.
  EXPECT_EQ(sent_msgs[3].header.type, CRAS_MAIN_METRICS);
  EXPECT_EQ(sent_msgs[3].header.length,
            sizeof(struct cras_server_metrics_message));
  EXPECT_EQ(sent_msgs[3].metrics_type, LONGEST_FETCH_DELAY);
  EXPECT_EQ(sent_msgs[3].data.stream_data.client_type, CRAS_CLIENT_TYPE_TEST);
  EXPECT_EQ(sent_msgs[3].data.stream_data.stream_type,
            CRAS_STREAM_TYPE_DEFAULT);
  EXPECT_EQ(sent_msgs[3].data.stream_data.direction, CRAS_STREAM_INPUT);
  EXPECT_EQ(sent_msgs[3].data.stream_data.runtime.tv_sec, 0);
  EXPECT_EQ(sent_msgs[3].data.stream_data.runtime.tv_nsec, 995000000);

  // Log number of fetch delays.
  EXPECT_EQ(sent_msgs[4].header.type, CRAS_MAIN_METRICS);
  EXPECT_EQ(sent_msgs[4].header.length,
            sizeof(struct cras_server_metrics_message));
  EXPECT_EQ(sent_msgs[4].metrics_type, FETCH_DELAY_COUNT);
  EXPECT_EQ(sent_msgs[4].data.stream_data.client_type, CRAS_CLIENT_TYPE_TEST);
  EXPECT_EQ(sent_msgs[4].data.stream_data.stream_type,
            CRAS_STREAM_TYPE_DEFAULT);
  EXPECT_EQ(sent_msgs[4].data.stream_data.direction, CRAS_STREAM_INPUT);
  EXPECT_EQ(sent_msgs[4].data.stream_data.count, 10);

  // Log number of overrun count.
  EXPECT_EQ(sent_msgs[5].header.type, CRAS_MAIN_METRICS);
  EXPECT_EQ(sent_msgs[5].header.length,
            sizeof(struct cras_server_metrics_message));
  EXPECT_EQ(sent_msgs[5].metrics_type, STREAM_OVERRUN_COUNT);
  EXPECT_EQ(sent_msgs[5].data.stream_data.client_type, CRAS_CLIENT_TYPE_TEST);
  EXPECT_EQ(sent_msgs[5].data.stream_data.stream_type,
            CRAS_STREAM_TYPE_DEFAULT);
  EXPECT_EQ(sent_msgs[5].data.stream_data.count, 3);

  // Log overrun frames.
  EXPECT_EQ(sent_msgs[6].header.type, CRAS_MAIN_METRICS);
  EXPECT_EQ(sent_msgs[6].header.length,
            sizeof(struct cras_server_metrics_message));
  EXPECT_EQ(sent_msgs[6].metrics_type, STREAM_OVERRUN_FRAMES);
  EXPECT_EQ(sent_msgs[6].data.stream_data.client_type, CRAS_CLIENT_TYPE_TEST);
  EXPECT_EQ(sent_msgs[6].data.stream_data.stream_type,
            CRAS_STREAM_TYPE_DEFAULT);
  EXPECT_EQ(sent_msgs[6].data.stream_data.count, 1000);

  free(stream.shm->header);
  free(stream.shm);
}

TEST(ServerMetricsTestSuite, SetMetricsBusyloop) {
  ResetStubData();
  struct timespec time = {40, 0};
  unsigned count = 3;

  cras_server_metrics_busyloop(&time, count);

  EXPECT_EQ(sent_msgs.size(), 1);
  EXPECT_EQ(sent_msgs[0].header.type, CRAS_MAIN_METRICS);
  EXPECT_EQ(sent_msgs[0].header.length,
            sizeof(struct cras_server_metrics_message));
  EXPECT_EQ(sent_msgs[0].metrics_type, BUSYLOOP);
  EXPECT_EQ(sent_msgs[0].data.timespec_data.runtime.tv_sec, 40);
  EXPECT_EQ(sent_msgs[0].data.timespec_data.runtime.tv_nsec, 0);
  EXPECT_EQ(sent_msgs[0].data.timespec_data.count, 3);
}

TEST(ServerMetricsTestSuite, SetMetricsBusyloopLength) {
  ResetStubData();
  unsigned length = 5;

  cras_server_metrics_busyloop_length(length);

  EXPECT_EQ(sent_msgs.size(), 1);
  EXPECT_EQ(sent_msgs[0].header.type, CRAS_MAIN_METRICS);
  EXPECT_EQ(sent_msgs[0].header.length,
            sizeof(struct cras_server_metrics_message));
  EXPECT_EQ(sent_msgs[0].metrics_type, BUSYLOOP_LENGTH);
  EXPECT_EQ(sent_msgs[0].data.value, 5);
}

struct HfpSrStatusTestParam {
  enum CRAS_NODE_TYPE node_type;
  enum CRAS_METRICS_DEVICE_TYPE device_type;
  enum CRAS_METRICS_HFP_MIC_SR_STATUS status;
};

class ServerMetricsHfpSrStatusTest
    : public testing::TestWithParam<HfpSrStatusTestParam> {
 protected:
  virtual void SetUp() { ResetStubData(); }
  virtual void TearDown() { sent_msgs.clear(); }
};

TEST_P(ServerMetricsHfpSrStatusTest, TestCrasServerMetricsHfpMicSrStatus) {
  const auto& param = ServerMetricsHfpSrStatusTest::GetParam();
  struct cras_iodev iodev;
  struct cras_ionode active_node;

  iodev.direction = CRAS_STREAM_INPUT;
  iodev.active_node = &active_node;
  active_node.type = GetParam().node_type;
  active_node.btflags = CRAS_BT_FLAG_HFP;

  cras_server_metrics_hfp_mic_sr_status(&iodev, GetParam().status);

  EXPECT_EQ(sent_msgs.size(), 1);
  EXPECT_EQ(sent_msgs[0].header.type, CRAS_MAIN_METRICS);
  EXPECT_EQ(sent_msgs[0].header.length,
            sizeof(struct cras_server_metrics_message));
  EXPECT_EQ(sent_msgs[0].metrics_type, BT_MIC_SUPER_RESOLUTION_STATUS);
  EXPECT_EQ(sent_msgs[0].data.device_data.type, GetParam().device_type);
  EXPECT_EQ(sent_msgs[0].data.device_data.value, param.status);
}

INSTANTIATE_TEST_SUITE_P(
    ,
    ServerMetricsHfpSrStatusTest,
    testing::Values(HfpSrStatusTestParam(
                        {.node_type = CRAS_NODE_TYPE_BLUETOOTH,
                         .device_type = CRAS_METRICS_DEVICE_BLUETOOTH_WB_MIC,
                         .status = CRAS_METRICS_HFP_MIC_SR_ENABLE_SUCCESS}),
                    HfpSrStatusTestParam(
                        {.node_type = CRAS_NODE_TYPE_BLUETOOTH_NB_MIC,
                         .device_type = CRAS_METRICS_DEVICE_BLUETOOTH_NB_MIC,
                         .status = CRAS_METRICS_HFP_MIC_SR_FEATURE_DISABLED})));

struct CrasDlcManagerTestParam {
  struct CrasDlcId128 dlc_id;
  std::string dlc_id_str;
  int success_time;
  std::vector<int> elapsed_seconds;
};

class ServerMetricsCrasDlcManagerTest
    : public testing::TestWithParam<CrasDlcManagerTestParam> {
 protected:
  virtual void SetUp() { ResetStubData(); }
  virtual void TearDown() { sent_msgs.clear(); }
};

TEST_P(ServerMetricsCrasDlcManagerTest, TestCrasServerMetricsDlcManagerStatus) {
  const auto& param = GetParam();
  const std::string prefix = "Cras.DlcManagerStatus";
  cras_system_state_in_main_thread_ret = 1;

  for (int elapsed_secs : param.elapsed_seconds) {
    cras_server_metrics_dlc_install_elapsed_time_on_failure(param.dlc_id,
                                                            elapsed_secs);

    ASSERT_EQ(cras_metrics_log_histogram_called_args.size(), 1);
    EXPECT_EQ(std::get<0>(cras_metrics_log_histogram_called_args.front()),
              prefix + ".ElapsedTimeHistogramOnFailure." + param.dlc_id_str);
    EXPECT_EQ(std::get<1>(cras_metrics_log_histogram_called_args.front()),
              elapsed_secs);
    cras_metrics_log_histogram_called_args.clear();
  }

  cras_server_metrics_dlc_install_elapsed_time_on_success(param.dlc_id,
                                                          param.success_time);

  ASSERT_EQ(cras_metrics_log_histogram_called_args.size(), 1);
  EXPECT_EQ(std::get<0>(cras_metrics_log_histogram_called_args.front()),
            prefix + ".ElapsedTimeHistogramOnSuccess." + param.dlc_id_str);
  EXPECT_EQ(std::get<1>(cras_metrics_log_histogram_called_args.front()),
            param.success_time);
}

INSTANTIATE_TEST_SUITE_P(
    ,
    ServerMetricsCrasDlcManagerTest,
    testing::Values(CrasDlcManagerTestParam({.dlc_id = {.id = "nc-ap-dlc"},
                                             .dlc_id_str = "nc-ap-dlc",
                                             .success_time = 0}),
                    CrasDlcManagerTestParam({.dlc_id = {.id = "sr-bt-dlc"},
                                             .dlc_id_str = "sr-bt-dlc",
                                             .success_time = 487,
                                             .elapsed_seconds = {0, 1, 3, 7, 15,
                                                                 31, 63, 127,
                                                                 247, 367}})));

extern "C" {

int cras_main_message_add_handler(enum CRAS_MAIN_MESSAGE_TYPE type,
                                  cras_message_callback callback,
                                  void* callback_data) {
  type_set = type;
  return 0;
}

void cras_metrics_log_histogram(const char* name,
                                int sample,
                                int min,
                                int max,
                                int nbuckets) {
  cras_metrics_log_histogram_called_args.emplace_back(name, sample, min, max,
                                                      nbuckets);
}

void cras_metrics_log_sparse_histogram(const char* name, int sample) {
  cras_metrics_log_sparse_histogram_called_args.emplace_back(name, sample);
}

int cras_main_message_send(struct cras_main_message* msg) {
  // Copy the sent message so we can examine it in the test later.
  struct cras_server_metrics_message sent_msg;
  memcpy(&sent_msg, msg, sizeof(sent_msg));
  sent_msgs.push_back(sent_msg);
  return 0;
}

int cras_system_state_in_main_thread() {
  return cras_system_state_in_main_thread_ret;
}

//  From librt.
int clock_gettime(clockid_t clk_id, struct timespec* tp) {
  tp->tv_sec = clock_gettime_retspec.tv_sec;
  tp->tv_nsec = clock_gettime_retspec.tv_nsec;
  return 0;
}

unsigned int cras_iodev_get_num_underruns(const struct cras_iodev* iodev) {
  return iodev->num_underruns;
}

unsigned int cras_iodev_get_num_underruns_during_nc(
    const struct cras_iodev* iodev) {
  return iodev->num_underruns_during_nc;
}

unsigned int cras_iodev_get_num_samples_dropped(
    const struct cras_iodev* iodev) {
  return iodev->num_samples_dropped;
}

}  // extern "C"
}  // namespace
