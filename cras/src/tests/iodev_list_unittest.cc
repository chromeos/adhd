// Copyright 2012 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <gtest/gtest.h>
#include <stdio.h>

#include <algorithm>
#include <map>
#include "cras_iodev_info.h"

extern "C" {
#include "audio_thread.h"
#include "cras_iodev.h"
#include "cras_iodev_list.c"
#include "cras_main_thread_log.h"
#include "cras_observer_ops.h"
#include "cras_ramp.h"
#include "cras_rstream.h"
#include "cras_server_metrics.h"
#include "cras_system_state.h"
#include "cras_tm.h"
#include "stream_list.h"
#include "utlist.h"
}

namespace {

struct cras_server_state server_state_stub;
struct cras_server_state* server_state_update_begin_return;
int system_get_mute_return;

/* Data for stubs. */
static struct cras_observer_ops* observer_ops;
static unsigned int set_node_plugged_called;
static cras_iodev* audio_thread_remove_streams_active_dev;
static cras_iodev* audio_thread_set_active_dev_val;
static int audio_thread_set_active_dev_called;
static cras_iodev* audio_thread_add_open_dev_dev;
static int audio_thread_add_open_dev_called;
static int audio_thread_rm_open_dev_called;
static int audio_thread_is_dev_open_ret;
static struct audio_thread thread;
static struct cras_iodev loopback_input;
static int cras_iodev_close_called;
static struct cras_iodev* cras_iodev_close_dev;
static struct cras_iodev mock_hotword_iodev;
static struct cras_iodev mock_empty_iodev[2];
static stream_callback* stream_add_cb;
static stream_callback* stream_rm_cb;
static struct cras_rstream* stream_list_get_ret;
static int server_stream_create_called;
static int server_stream_destroy_called;
static int audio_thread_drain_stream_return;
static int audio_thread_drain_stream_called;
static int cras_tm_create_timer_called;
static int cras_tm_cancel_timer_called;
static void (*cras_tm_timer_cb)(struct cras_timer* t, void* data);
static void* cras_tm_timer_cb_data;
static struct timespec clock_gettime_retspec;
static struct cras_iodev* device_enabled_dev;
static int device_enabled_count;
static struct cras_iodev* device_disabled_dev;
static int device_disabled_count;
static void* device_enabled_cb_data;
static void* device_disabled_cb_data;
static struct cras_rstream* audio_thread_add_stream_stream;
static struct cras_iodev* audio_thread_add_stream_dev;
static struct cras_iodev* audio_thread_disconnect_stream_dev;
static int audio_thread_add_stream_called;
static unsigned update_active_node_called;
static struct cras_iodev* update_active_node_iodev_val[5];
static unsigned update_active_node_node_idx_val[5];
static unsigned update_active_node_dev_enabled_val[5];
static int set_swap_mode_for_node_called;
static int set_swap_mode_for_node_enable;
static int set_display_rotation_for_node_called;
static enum CRAS_SCREEN_ROTATION display_rotation;
static int cras_iodev_start_volume_ramp_called;
static size_t cras_observer_add_called;
static size_t cras_observer_remove_called;
static size_t cras_observer_notify_nodes_called;
static size_t cras_observer_notify_active_node_called;
static size_t cras_observer_notify_output_node_volume_called;
static size_t cras_observer_notify_node_left_right_swapped_called;
static size_t cras_observer_notify_input_node_gain_called;
static int cras_iodev_open_called;
static int cras_iodev_open_ret[8];
static struct cras_audio_format cras_iodev_open_fmt;
static int set_mute_called;
static std::vector<struct cras_iodev*> set_mute_dev_vector;
static std::vector<unsigned int> audio_thread_dev_start_ramp_dev_vector;
static int audio_thread_dev_start_ramp_called;
static enum CRAS_IODEV_RAMP_REQUEST audio_thread_dev_start_ramp_req;
static std::map<int, bool> stream_list_has_pinned_stream_ret;
static struct cras_rstream* audio_thread_disconnect_stream_stream;
static int audio_thread_disconnect_stream_called;
static struct cras_iodev fake_sco_in_dev, fake_sco_out_dev;
static struct cras_ionode fake_sco_in_node, fake_sco_out_node;
static int server_state_hotword_pause_at_suspend;
static int cras_system_get_max_internal_mic_gain_return;
static int cras_stream_apm_set_aec_ref_called;
static int cras_stream_apm_remove_called;
static int cras_stream_apm_add_called;

int dev_idx_in_vector(std::vector<unsigned int> v, unsigned int idx) {
  return std::find(v.begin(), v.end(), idx) != v.end();
}

int device_in_vector(std::vector<struct cras_iodev*> v,
                     struct cras_iodev* dev) {
  return std::find(v.begin(), v.end(), dev) != v.end();
}

class IoDevTestSuite : public testing::Test {
 protected:
  virtual void SetUp() {
    cras_iodev_list_reset();

    cras_iodev_close_called = 0;
    stream_list_get_ret = 0;
    server_stream_create_called = 0;
    server_stream_destroy_called = 0;
    audio_thread_drain_stream_return = 0;
    audio_thread_drain_stream_called = 0;
    cras_tm_create_timer_called = 0;
    cras_tm_cancel_timer_called = 0;

    audio_thread_disconnect_stream_called = 0;
    audio_thread_disconnect_stream_stream = NULL;
    audio_thread_is_dev_open_ret = 0;
    stream_list_has_pinned_stream_ret.clear();

    sample_rates_[0] = 44100;
    sample_rates_[1] = 48000;
    sample_rates_[2] = 0;

    channel_counts_[0] = 2;
    channel_counts_[1] = 0;

    fmt_.format = SND_PCM_FORMAT_S16_LE;
    fmt_.frame_rate = 48000;
    fmt_.num_channels = 2;

    memset(&d1_, 0, sizeof(d1_));
    memset(&d2_, 0, sizeof(d2_));
    memset(&d3_, 0, sizeof(d3_));

    memset(&node1, 0, sizeof(node1));
    memset(&node2, 0, sizeof(node2));
    memset(&node3, 0, sizeof(node3));
    memset(&clock_gettime_retspec, 0, sizeof(clock_gettime_retspec));

    d1_.set_volume = NULL;
    d1_.set_capture_gain = NULL;
    d1_.set_capture_mute = NULL;
    d1_.update_supported_formats = NULL;
    d1_.update_active_node = update_active_node;
    d1_.set_swap_mode_for_node = set_swap_mode_for_node;
    d1_.set_display_rotation_for_node = set_node_display_rotation;
    d1_.format = NULL;
    d1_.direction = CRAS_STREAM_OUTPUT;
    d1_.info.idx = -999;
    d1_.nodes = &node1;
    d1_.active_node = &node1;
    strcpy(d1_.info.name, "d1");
    d1_.supported_rates = sample_rates_;
    d1_.supported_channel_counts = channel_counts_;
    d2_.set_volume = NULL;
    d2_.set_capture_gain = NULL;
    d2_.set_capture_mute = NULL;
    d2_.update_supported_formats = NULL;
    d2_.update_active_node = update_active_node;
    d2_.format = NULL;
    d2_.direction = CRAS_STREAM_OUTPUT;
    d2_.info.idx = -999;
    d2_.nodes = &node2;
    d2_.active_node = &node2;
    strcpy(d2_.info.name, "d2");
    d2_.supported_rates = sample_rates_;
    d2_.supported_channel_counts = channel_counts_;
    d3_.set_volume = NULL;
    d3_.set_capture_gain = NULL;
    d3_.set_capture_mute = NULL;
    d3_.update_supported_formats = NULL;
    d3_.update_active_node = update_active_node;
    d3_.format = NULL;
    d3_.direction = CRAS_STREAM_OUTPUT;
    d3_.info.idx = -999;
    d3_.nodes = &node3;
    d3_.active_node = &node3;
    strcpy(d3_.info.name, "d3");
    d3_.supported_rates = sample_rates_;
    d3_.supported_channel_counts = channel_counts_;

    loopback_input.set_volume = NULL;
    loopback_input.set_capture_gain = NULL;
    loopback_input.set_capture_mute = NULL;
    loopback_input.update_supported_formats = NULL;
    loopback_input.update_active_node = update_active_node;
    loopback_input.format = NULL;
    loopback_input.direction = CRAS_STREAM_INPUT;
    loopback_input.info.idx = -999;
    loopback_input.nodes = &node3;
    loopback_input.active_node = &node3;
    strcpy(loopback_input.info.name, "loopback_input");
    loopback_input.supported_rates = sample_rates_;
    loopback_input.supported_channel_counts = channel_counts_;

    server_state_update_begin_return = &server_state_stub;
    system_get_mute_return = false;

    /* Reset stub data. */
    set_node_plugged_called = 0;
    audio_thread_rm_open_dev_called = 0;
    audio_thread_add_open_dev_called = 0;
    audio_thread_set_active_dev_called = 0;
    audio_thread_add_stream_called = 0;
    update_active_node_called = 0;
    cras_observer_add_called = 0;
    cras_observer_remove_called = 0;
    cras_observer_notify_nodes_called = 0;
    cras_observer_notify_active_node_called = 0;
    cras_observer_notify_output_node_volume_called = 0;
    cras_observer_notify_node_left_right_swapped_called = 0;
    cras_observer_notify_input_node_gain_called = 0;
    cras_iodev_open_called = 0;
    memset(cras_iodev_open_ret, 0, sizeof(cras_iodev_open_ret));
    set_mute_called = 0;
    set_mute_dev_vector.clear();
    set_swap_mode_for_node_called = 0;
    set_swap_mode_for_node_enable = 0;
    set_display_rotation_for_node_called = 0;
    cras_iodev_start_volume_ramp_called = 0;
    audio_thread_dev_start_ramp_dev_vector.clear();
    audio_thread_dev_start_ramp_called = 0;
    audio_thread_dev_start_ramp_req = CRAS_IODEV_RAMP_REQUEST_UP_START_PLAYBACK;
    for (int i = 0; i < 5; i++)
      update_active_node_iodev_val[i] = NULL;
    DL_APPEND(fake_sco_in_dev.nodes, &fake_sco_in_node);
    DL_APPEND(fake_sco_out_dev.nodes, &fake_sco_out_node);
    fake_sco_in_node.btflags &= ~CRAS_BT_FLAG_SCO_OFFLOAD;
    fake_sco_out_node.btflags &= ~CRAS_BT_FLAG_SCO_OFFLOAD;
    mock_empty_iodev[0].state = CRAS_IODEV_STATE_CLOSE;
    mock_empty_iodev[0].update_active_node = update_active_node;
    mock_empty_iodev[1].state = CRAS_IODEV_STATE_CLOSE;
    mock_empty_iodev[1].update_active_node = update_active_node;
    mock_hotword_iodev.update_active_node = update_active_node;
    server_state_hotword_pause_at_suspend = 0;
    cras_system_get_max_internal_mic_gain_return = DEFAULT_MAX_INPUT_NODE_GAIN;
  }

  virtual void TearDown() { cras_iodev_list_reset(); }

  static void set_volume_1(struct cras_iodev* iodev) { set_volume_1_called_++; }

  static void set_capture_gain_1(struct cras_iodev* iodev) {
    set_capture_gain_1_called_++;
  }

  static void set_capture_mute_1(struct cras_iodev* iodev) {
    set_capture_mute_1_called_++;
  }

  static void update_active_node(struct cras_iodev* iodev,
                                 unsigned node_idx,
                                 unsigned dev_enabled) {
    int i = update_active_node_called++ % 5;
    update_active_node_iodev_val[i] = iodev;
    update_active_node_node_idx_val[i] = node_idx;
    update_active_node_dev_enabled_val[i] = dev_enabled;
  }

  static void set_active_node_by_id(struct cras_iodev* iodev,
                                    unsigned node_idx,
                                    unsigned dev_enabled) {
    struct cras_ionode* n;

    update_active_node_called++;

    DL_FOREACH (iodev->nodes, n) {
      if (n->idx == node_idx) {
        iodev->active_node = n;
        return;
      }
    }
  }

  static int set_node_display_rotation(struct cras_iodev* iodev,
                                       struct cras_ionode* node,
                                       enum CRAS_SCREEN_ROTATION rotation) {
    set_display_rotation_for_node_called++;
    display_rotation = rotation;
    return 0;
  }

  static int set_swap_mode_for_node(struct cras_iodev* iodev,
                                    struct cras_ionode* node,
                                    int enable) {
    set_swap_mode_for_node_called++;
    set_swap_mode_for_node_enable = enable;
    return 0;
  }

  struct cras_iodev d1_;
  struct cras_iodev d2_;
  struct cras_iodev d3_;
  struct cras_audio_format fmt_;
  size_t sample_rates_[3];
  size_t channel_counts_[2];
  static int set_volume_1_called_;
  static int set_capture_gain_1_called_;
  static int set_capture_mute_1_called_;
  struct cras_ionode node1, node2, node3;
};

int IoDevTestSuite::set_volume_1_called_;
int IoDevTestSuite::set_capture_gain_1_called_;
int IoDevTestSuite::set_capture_mute_1_called_;

// Check that Init registers observer client. */
TEST_F(IoDevTestSuite, InitSetup) {
  cras_iodev_list_init();
  EXPECT_EQ(1, cras_observer_add_called);
  cras_iodev_list_deinit();
  EXPECT_EQ(1, cras_observer_remove_called);
}

/* Check that the suspend alert from cras_system will trigger suspend
 * and resume call of all iodevs. */
TEST_F(IoDevTestSuite, SetSuspendResume) {
  struct cras_rstream rstream, rstream2, rstream3;
  struct cras_rstream* stream_list = NULL;
  int rc;

  memset(&rstream, 0, sizeof(rstream));
  memset(&rstream2, 0, sizeof(rstream2));
  memset(&rstream3, 0, sizeof(rstream3));

  cras_iodev_list_init();

  d1_.direction = CRAS_STREAM_OUTPUT;
  rc = cras_iodev_list_add_output(&d1_);
  ASSERT_EQ(0, rc);

  d1_.format = &fmt_;

  audio_thread_add_open_dev_called = 0;
  cras_iodev_list_add_active_node(CRAS_STREAM_OUTPUT,
                                  cras_make_node_id(d1_.info.idx, 1));
  DL_APPEND(stream_list, &rstream);
  stream_add_cb(&rstream);
  EXPECT_EQ(1, audio_thread_add_stream_called);
  EXPECT_EQ(1, audio_thread_add_open_dev_called);

  DL_APPEND(stream_list, &rstream2);
  stream_add_cb(&rstream2);
  EXPECT_EQ(2, audio_thread_add_stream_called);

  audio_thread_rm_open_dev_called = 0;
  observer_ops->suspend_changed(NULL, 1);
  EXPECT_EQ(1, audio_thread_rm_open_dev_called);

  /* Test disable/enable dev won't cause add_stream to audio_thread. */
  audio_thread_add_stream_called = 0;
  cras_iodev_list_disable_dev(&d1_, false);
  cras_iodev_list_enable_dev(&d1_);
  EXPECT_EQ(0, audio_thread_add_stream_called);

  audio_thread_drain_stream_return = 0;
  DL_DELETE(stream_list, &rstream2);
  stream_rm_cb(&rstream2);
  EXPECT_EQ(1, audio_thread_drain_stream_called);

  /* Test stream_add_cb won't cause add_stream to audio_thread. */
  audio_thread_add_stream_called = 0;
  DL_APPEND(stream_list, &rstream3);
  stream_add_cb(&rstream3);
  EXPECT_EQ(0, audio_thread_add_stream_called);

  audio_thread_add_open_dev_called = 0;
  audio_thread_add_stream_called = 0;
  stream_list_get_ret = stream_list;
  observer_ops->suspend_changed(NULL, 0);
  EXPECT_EQ(1, audio_thread_add_open_dev_called);
  EXPECT_EQ(2, audio_thread_add_stream_called);
  EXPECT_EQ(&rstream3, audio_thread_add_stream_stream);

  cras_iodev_list_deinit();
  EXPECT_EQ(3, cras_observer_notify_active_node_called);
}

/* Check that the suspend/resume call of active iodev will be triggered and
 * fallback device will be transciently enabled while adding a new stream whose
 * channel count is higher than the active iodev. */
TEST_F(IoDevTestSuite, ReopenDevForHigherChannels) {
  struct cras_rstream rstream, rstream2;
  struct cras_rstream* stream_list = NULL;
  int rc;

  memset(&rstream, 0, sizeof(rstream));
  memset(&rstream2, 0, sizeof(rstream2));
  rstream.format = fmt_;
  rstream2.format = fmt_;
  rstream2.format.num_channels = 6;

  cras_iodev_list_init();

  d1_.direction = CRAS_STREAM_OUTPUT;
  rc = cras_iodev_list_add_output(&d1_);
  ASSERT_EQ(0, rc);

  d1_.format = &fmt_;
  d1_.info.max_supported_channels = 2;

  audio_thread_add_open_dev_called = 0;
  cras_iodev_list_add_active_node(CRAS_STREAM_OUTPUT,
                                  cras_make_node_id(d1_.info.idx, 1));
  DL_APPEND(stream_list, &rstream);
  stream_list_get_ret = stream_list;
  stream_add_cb(&rstream);
  EXPECT_EQ(1, audio_thread_add_stream_called);
  EXPECT_EQ(1, audio_thread_add_open_dev_called);
  EXPECT_EQ(1, cras_iodev_open_called);
  EXPECT_EQ(2, cras_iodev_open_fmt.num_channels);

  audio_thread_add_stream_called = 0;
  audio_thread_add_open_dev_called = 0;
  cras_iodev_open_called = 0;

  /* stream_list should be descending ordered by channel count. */
  DL_PREPEND(stream_list, &rstream2);
  stream_list_get_ret = stream_list;
  stream_add_cb(&rstream2);
  /* The channel count(=6) of rstream2 exceeds d1's max_supported_channels(=2),
   * rstream2 will be added directly to d1, which will not be re-opened. */
  EXPECT_EQ(1, audio_thread_add_stream_called);
  EXPECT_EQ(0, audio_thread_add_open_dev_called);
  EXPECT_EQ(0, cras_iodev_open_called);

  d1_.info.max_supported_channels = 6;
  stream_rm_cb(&rstream2);

  audio_thread_add_stream_called = 0;
  audio_thread_add_open_dev_called = 0;
  cras_iodev_open_called = 0;

  stream_add_cb(&rstream2);
  /* Added both rstreams to fallback device, then re-opened d1. */
  EXPECT_EQ(4, audio_thread_add_stream_called);
  EXPECT_EQ(2, audio_thread_add_open_dev_called);
  EXPECT_EQ(2, cras_iodev_open_called);
  EXPECT_EQ(6, cras_iodev_open_fmt.num_channels);

  cras_iodev_list_deinit();
}

/* Check that after resume, all output devices enter ramp mute state if there is
 * any output stream. */
TEST_F(IoDevTestSuite, RampMuteAfterResume) {
  struct cras_rstream rstream, rstream2;
  struct cras_rstream* stream_list = NULL;
  int rc;

  memset(&rstream, 0, sizeof(rstream));

  cras_iodev_list_init();

  d1_.direction = CRAS_STREAM_OUTPUT;
  d1_.initial_ramp_request = CRAS_IODEV_RAMP_REQUEST_UP_START_PLAYBACK;
  rc = cras_iodev_list_add_output(&d1_);
  ASSERT_EQ(0, rc);

  d2_.direction = CRAS_STREAM_INPUT;
  d2_.initial_ramp_request = CRAS_IODEV_RAMP_REQUEST_UP_START_PLAYBACK;
  rc = cras_iodev_list_add_input(&d2_);
  ASSERT_EQ(0, rc);

  d1_.format = &fmt_;
  d2_.format = &fmt_;

  audio_thread_add_open_dev_called = 0;
  cras_iodev_list_add_active_node(CRAS_STREAM_OUTPUT,
                                  cras_make_node_id(d1_.info.idx, 1));

  rstream.direction = CRAS_STREAM_OUTPUT;
  DL_APPEND(stream_list, &rstream);
  stream_add_cb(&rstream);
  EXPECT_EQ(1, audio_thread_add_stream_called);
  EXPECT_EQ(1, audio_thread_add_open_dev_called);

  rstream2.direction = CRAS_STREAM_INPUT;
  DL_APPEND(stream_list, &rstream2);
  stream_add_cb(&rstream2);

  /* Suspend and resume */
  observer_ops->suspend_changed(NULL, 1);
  stream_list_get_ret = stream_list;
  observer_ops->suspend_changed(NULL, 0);

  /* Test only output device that has stream will be muted after resume */
  EXPECT_EQ(d1_.initial_ramp_request, CRAS_IODEV_RAMP_REQUEST_RESUME_MUTE);
  EXPECT_EQ(CRAS_IODEV_RAMP_REQUEST_UP_START_PLAYBACK,
            d2_.initial_ramp_request);

  /* Reset d1 ramp_mute and remove output stream to test again */
  d1_.initial_ramp_request = CRAS_IODEV_RAMP_REQUEST_UP_START_PLAYBACK;
  DL_DELETE(stream_list, &rstream);
  stream_list_get_ret = stream_list;
  stream_rm_cb(&rstream);

  /* Suspend and resume */
  observer_ops->suspend_changed(NULL, 1);
  stream_list_get_ret = stream_list;
  observer_ops->suspend_changed(NULL, 0);

  EXPECT_EQ(CRAS_IODEV_RAMP_REQUEST_UP_START_PLAYBACK,
            d1_.initial_ramp_request);
  EXPECT_EQ(CRAS_IODEV_RAMP_REQUEST_UP_START_PLAYBACK,
            d2_.initial_ramp_request);

  cras_iodev_list_deinit();
}

TEST_F(IoDevTestSuite, InitDevFailShouldEnableFallback) {
  int rc;
  struct cras_rstream rstream;
  struct cras_rstream* stream_list = NULL;

  memset(&rstream, 0, sizeof(rstream));
  cras_iodev_list_init();

  d1_.direction = CRAS_STREAM_OUTPUT;
  rc = cras_iodev_list_add_output(&d1_);
  ASSERT_EQ(0, rc);

  d1_.format = &fmt_;

  cras_iodev_list_select_node(CRAS_STREAM_OUTPUT,
                              cras_make_node_id(d1_.info.idx, 0));

  cras_iodev_open_ret[0] = -5;
  cras_iodev_open_ret[1] = 0;

  DL_APPEND(stream_list, &rstream);
  stream_list_get_ret = stream_list;
  stream_add_cb(&rstream);
  /* open dev called twice, one for fallback device. */
  EXPECT_EQ(2, cras_iodev_open_called);
  EXPECT_EQ(1, audio_thread_add_stream_called);
  cras_iodev_list_deinit();
}

TEST_F(IoDevTestSuite, InitDevWithEchoRef) {
  int rc;
  struct cras_rstream rstream;
  struct cras_rstream* stream_list = NULL;

  memset(&rstream, 0, sizeof(rstream));
  cras_iodev_list_init();

  d1_.direction = CRAS_STREAM_OUTPUT;
  d1_.echo_reference_dev = &d2_;
  rc = cras_iodev_list_add_output(&d1_);
  ASSERT_EQ(0, rc);

  d2_.direction = CRAS_STREAM_INPUT;
  snprintf(d2_.active_node->name, CRAS_NODE_NAME_BUFFER_SIZE, "echo ref");
  rc = cras_iodev_list_add_input(&d2_);
  ASSERT_EQ(0, rc);

  d1_.format = &fmt_;
  d2_.format = &fmt_;

  cras_iodev_list_select_node(CRAS_STREAM_OUTPUT,
                              cras_make_node_id(d1_.info.idx, 0));
  /* No close call happened, because no stream exists. */
  EXPECT_EQ(0, cras_iodev_close_called);

  cras_iodev_open_ret[1] = 0;

  DL_APPEND(stream_list, &rstream);
  stream_list_get_ret = stream_list;
  stream_add_cb(&rstream);

  EXPECT_EQ(1, cras_iodev_open_called);
  EXPECT_EQ(1, server_stream_create_called);
  EXPECT_EQ(1, audio_thread_add_stream_called);

  DL_DELETE(stream_list, &rstream);
  stream_list_get_ret = stream_list;
  stream_rm_cb(&rstream);

  clock_gettime_retspec.tv_sec = 11;
  clock_gettime_retspec.tv_nsec = 0;
  cras_tm_timer_cb(NULL, NULL);

  EXPECT_EQ(1, cras_iodev_close_called);
  EXPECT_EQ(1, server_stream_destroy_called);

  cras_iodev_list_deinit();
}

TEST_F(IoDevTestSuite, SelectNodeOpenFailShouldScheduleRetry) {
  struct cras_rstream rstream;
  struct cras_rstream* stream_list = NULL;
  int rc;

  memset(&rstream, 0, sizeof(rstream));
  cras_iodev_list_init();

  d1_.direction = CRAS_STREAM_OUTPUT;
  rc = cras_iodev_list_add_output(&d1_);
  ASSERT_EQ(0, rc);

  d2_.direction = CRAS_STREAM_OUTPUT;
  rc = cras_iodev_list_add_output(&d2_);
  ASSERT_EQ(0, rc);

  d1_.format = &fmt_;
  d2_.format = &fmt_;

  cras_iodev_list_select_node(CRAS_STREAM_OUTPUT,
                              cras_make_node_id(d1_.info.idx, 1));
  DL_APPEND(stream_list, &rstream);
  stream_list_get_ret = stream_list;
  stream_add_cb(&rstream);

  /* Select node triggers: fallback open, d1 close, d2 open, fallback close. */
  cras_iodev_close_called = 0;
  cras_iodev_open_called = 0;
  cras_iodev_list_select_node(CRAS_STREAM_OUTPUT,
                              cras_make_node_id(d2_.info.idx, 1));
  EXPECT_EQ(2, cras_iodev_close_called);
  EXPECT_EQ(2, cras_iodev_open_called);
  EXPECT_EQ(0, cras_tm_create_timer_called);
  EXPECT_EQ(0, cras_tm_cancel_timer_called);

  /* Test that if select to d1 and open d1 fail, fallback doesn't close. */
  cras_iodev_open_called = 0;
  cras_iodev_open_ret[0] = 0;
  cras_iodev_open_ret[1] = -5;
  cras_iodev_open_ret[2] = 0;
  cras_tm_timer_cb = NULL;
  cras_iodev_list_select_node(CRAS_STREAM_OUTPUT,
                              cras_make_node_id(d1_.info.idx, 1));
  EXPECT_EQ(3, cras_iodev_close_called);
  EXPECT_EQ(&d2_, cras_iodev_close_dev);
  EXPECT_EQ(2, cras_iodev_open_called);
  EXPECT_EQ(0, cras_tm_cancel_timer_called);

  /* Assert a timer is scheduled to retry open. */
  EXPECT_NE((void*)NULL, cras_tm_timer_cb);
  EXPECT_EQ(1, cras_tm_create_timer_called);

  audio_thread_add_stream_called = 0;
  cras_tm_timer_cb(NULL, cras_tm_timer_cb_data);
  EXPECT_EQ(3, cras_iodev_open_called);
  EXPECT_EQ(1, audio_thread_add_stream_called);

  /* Retry open success will close fallback dev. */
  EXPECT_EQ(4, cras_iodev_close_called);
  EXPECT_EQ(0, cras_tm_cancel_timer_called);

  /* Select to d2 and fake an open failure. */
  cras_iodev_close_called = 0;
  cras_iodev_open_called = 0;
  cras_iodev_open_ret[0] = 0;
  cras_iodev_open_ret[1] = -5;
  cras_iodev_open_ret[2] = 0;
  cras_iodev_list_select_node(CRAS_STREAM_OUTPUT,
                              cras_make_node_id(d2_.info.idx, 1));
  EXPECT_EQ(1, cras_iodev_close_called);
  EXPECT_EQ(&d1_, cras_iodev_close_dev);
  EXPECT_EQ(2, cras_tm_create_timer_called);
  EXPECT_NE((void*)NULL, cras_tm_timer_cb);

  /* Select to another iodev should cancel the timer. */
  memset(cras_iodev_open_ret, 0, sizeof(cras_iodev_open_ret));
  cras_iodev_list_select_node(CRAS_STREAM_OUTPUT,
                              cras_make_node_id(d2_.info.idx, 1));
  EXPECT_EQ(1, cras_tm_cancel_timer_called);
  cras_iodev_list_deinit();
}

TEST_F(IoDevTestSuite, InitDevFailShouldScheduleRetry) {
  int rc;
  struct cras_rstream rstream;
  struct cras_rstream* stream_list = NULL;

  memset(&rstream, 0, sizeof(rstream));
  rstream.format = fmt_;
  cras_iodev_list_init();

  d1_.direction = CRAS_STREAM_OUTPUT;
  rc = cras_iodev_list_add_output(&d1_);
  ASSERT_EQ(0, rc);

  d1_.format = &fmt_;

  cras_iodev_list_select_node(CRAS_STREAM_OUTPUT,
                              cras_make_node_id(d1_.info.idx, 0));

  update_active_node_called = 0;
  cras_iodev_open_ret[0] = -5;
  cras_iodev_open_ret[1] = 0;
  cras_tm_timer_cb = NULL;
  DL_APPEND(stream_list, &rstream);
  stream_list_get_ret = stream_list;
  stream_add_cb(&rstream);
  /* open dev called twice, one for fallback device. */
  EXPECT_EQ(2, cras_iodev_open_called);
  EXPECT_EQ(1, audio_thread_add_stream_called);
  EXPECT_EQ(0, update_active_node_called);
  EXPECT_EQ(&mock_empty_iodev[CRAS_STREAM_OUTPUT], audio_thread_add_stream_dev);

  EXPECT_NE((void*)NULL, cras_tm_timer_cb);
  EXPECT_EQ(1, cras_tm_create_timer_called);

  /* If retry still fail, won't schedule more retry. */
  cras_iodev_open_ret[2] = -5;
  cras_tm_timer_cb(NULL, cras_tm_timer_cb_data);
  EXPECT_EQ(1, cras_tm_create_timer_called);
  EXPECT_EQ(1, audio_thread_add_stream_called);

  mock_empty_iodev[CRAS_STREAM_OUTPUT].format = &fmt_;
  cras_tm_timer_cb = NULL;
  cras_iodev_open_ret[3] = -5;
  stream_add_cb(&rstream);
  EXPECT_NE((void*)NULL, cras_tm_timer_cb);
  EXPECT_EQ(2, cras_tm_create_timer_called);

  cras_iodev_list_rm_output(&d1_);
  EXPECT_EQ(1, cras_tm_cancel_timer_called);
  cras_iodev_list_deinit();
}

TEST_F(IoDevTestSuite, PinnedStreamInitFailShouldScheduleRetry) {
  int rc;
  struct cras_rstream rstream;
  struct cras_rstream* stream_list = NULL;

  memset(&rstream, 0, sizeof(rstream));
  cras_iodev_list_init();

  d1_.direction = CRAS_STREAM_OUTPUT;
  rc = cras_iodev_list_add_output(&d1_);
  ASSERT_EQ(0, rc);

  d1_.format = &fmt_;

  rstream.is_pinned = 1;
  rstream.pinned_dev_idx = d1_.info.idx;

  cras_iodev_open_ret[0] = -5;
  cras_iodev_open_ret[1] = 0;
  cras_tm_timer_cb = NULL;
  DL_APPEND(stream_list, &rstream);
  stream_list_get_ret = stream_list;
  stream_add_cb(&rstream);
  /* Init pinned dev fail, not proceed to add stream. */
  EXPECT_EQ(1, cras_iodev_open_called);
  EXPECT_EQ(0, audio_thread_add_stream_called);

  EXPECT_NE((void*)NULL, cras_tm_timer_cb);
  EXPECT_EQ(1, cras_tm_create_timer_called);

  cras_tm_timer_cb(NULL, cras_tm_timer_cb_data);
  EXPECT_EQ(2, cras_iodev_open_called);
  EXPECT_EQ(1, audio_thread_add_stream_called);

  cras_iodev_list_rm_output(&d1_);
  cras_iodev_list_deinit();
}

static void device_enabled_cb(struct cras_iodev* dev, void* cb_data) {
  device_enabled_dev = dev;
  device_enabled_count++;
  device_enabled_cb_data = cb_data;
}

static void device_disabled_cb(struct cras_iodev* dev, void* cb_data) {
  device_disabled_dev = dev;
  device_disabled_count++;
  device_disabled_cb_data = cb_data;
}

TEST_F(IoDevTestSuite, SelectNode) {
  struct cras_rstream rstream, rstream2;
  int rc;

  memset(&rstream, 0, sizeof(rstream));
  memset(&rstream2, 0, sizeof(rstream2));

  cras_iodev_list_init();

  d1_.direction = CRAS_STREAM_OUTPUT;
  node1.idx = 1;
  rc = cras_iodev_list_add_output(&d1_);
  ASSERT_EQ(0, rc);

  d2_.direction = CRAS_STREAM_OUTPUT;
  node2.idx = 2;
  rc = cras_iodev_list_add_output(&d2_);
  ASSERT_EQ(0, rc);

  d1_.format = &fmt_;
  d2_.format = &fmt_;

  audio_thread_add_open_dev_called = 0;
  audio_thread_rm_open_dev_called = 0;

  device_enabled_count = 0;
  device_disabled_count = 0;

  EXPECT_EQ(0, cras_iodev_list_set_device_enabled_callback(
                   device_enabled_cb, device_disabled_cb, NULL, (void*)0xABCD));

  cras_iodev_list_add_active_node(CRAS_STREAM_OUTPUT,
                                  cras_make_node_id(d1_.info.idx, 1));

  EXPECT_EQ(1, device_enabled_count);
  EXPECT_EQ(1, cras_observer_notify_active_node_called);
  EXPECT_EQ(&d1_, cras_iodev_list_get_first_enabled_iodev(CRAS_STREAM_OUTPUT));

  // There should be a disable device call for the fallback device.
  // But no close call actually happened, because no stream exists.
  EXPECT_EQ(0, audio_thread_rm_open_dev_called);
  EXPECT_EQ(1, device_disabled_count);
  EXPECT_NE(&d1_, device_disabled_dev);

  DL_APPEND(stream_list_get_ret, &rstream);
  stream_add_cb(&rstream);

  EXPECT_EQ(1, audio_thread_add_stream_called);
  EXPECT_EQ(1, audio_thread_add_open_dev_called);

  DL_APPEND(stream_list_get_ret, &rstream2);
  stream_add_cb(&rstream2);

  EXPECT_EQ(2, audio_thread_add_stream_called);
  EXPECT_EQ(1, audio_thread_add_open_dev_called);

  cras_iodev_list_select_node(CRAS_STREAM_OUTPUT,
                              cras_make_node_id(d2_.info.idx, 2));

  // Additional enabled devices: fallback device, d2_.
  EXPECT_EQ(3, device_enabled_count);
  // Additional disabled devices: d1_, fallback device.
  EXPECT_EQ(3, device_disabled_count);
  EXPECT_EQ(2, audio_thread_rm_open_dev_called);
  EXPECT_EQ(2, cras_observer_notify_active_node_called);
  EXPECT_EQ(&d2_, cras_iodev_list_get_first_enabled_iodev(CRAS_STREAM_OUTPUT));

  // For each stream, the stream is added for fallback device and d2_.
  EXPECT_EQ(6, audio_thread_add_stream_called);

  EXPECT_EQ(
      0, cras_iodev_list_set_device_enabled_callback(NULL, NULL, NULL, NULL));
  cras_iodev_list_deinit();
}

TEST_F(IoDevTestSuite, SelectPreviouslyEnabledNode) {
  struct cras_rstream rstream;
  int rc;

  memset(&rstream, 0, sizeof(rstream));

  cras_iodev_list_init();

  d1_.direction = CRAS_STREAM_OUTPUT;
  node1.idx = 1;
  rc = cras_iodev_list_add_output(&d1_);
  ASSERT_EQ(0, rc);

  d2_.direction = CRAS_STREAM_OUTPUT;
  node2.idx = 2;
  rc = cras_iodev_list_add_output(&d2_);
  ASSERT_EQ(0, rc);

  d1_.format = &fmt_;
  d2_.format = &fmt_;

  audio_thread_add_open_dev_called = 0;
  audio_thread_rm_open_dev_called = 0;
  device_enabled_count = 0;
  device_disabled_count = 0;

  EXPECT_EQ(0, cras_iodev_list_set_device_enabled_callback(
                   device_enabled_cb, device_disabled_cb, NULL, (void*)0xABCD));

  // Add an active node.
  cras_iodev_list_add_active_node(CRAS_STREAM_OUTPUT,
                                  cras_make_node_id(d1_.info.idx, 1));

  EXPECT_EQ(1, device_enabled_count);
  EXPECT_EQ(1, cras_observer_notify_active_node_called);
  EXPECT_EQ(&d1_, cras_iodev_list_get_first_enabled_iodev(CRAS_STREAM_OUTPUT));

  // There should be a disable device call for the fallback device.
  EXPECT_EQ(1, device_disabled_count);
  EXPECT_NE(&d1_, device_disabled_dev);
  EXPECT_NE(&d2_, device_disabled_dev);

  DL_APPEND(stream_list_get_ret, &rstream);
  stream_add_cb(&rstream);

  EXPECT_EQ(1, audio_thread_add_open_dev_called);
  EXPECT_EQ(1, audio_thread_add_stream_called);

  // Add a second active node.
  cras_iodev_list_add_active_node(CRAS_STREAM_OUTPUT,
                                  cras_make_node_id(d2_.info.idx, 2));

  EXPECT_EQ(2, device_enabled_count);
  EXPECT_EQ(1, device_disabled_count);
  EXPECT_EQ(2, cras_observer_notify_active_node_called);
  EXPECT_EQ(&d1_, cras_iodev_list_get_first_enabled_iodev(CRAS_STREAM_OUTPUT));

  EXPECT_EQ(2, audio_thread_add_open_dev_called);
  EXPECT_EQ(2, audio_thread_add_stream_called);
  EXPECT_EQ(0, audio_thread_rm_open_dev_called);

  // Select the second added active node - the initially added node should get
  // disabled.
  cras_iodev_list_select_node(CRAS_STREAM_OUTPUT,
                              cras_make_node_id(d2_.info.idx, 2));

  EXPECT_EQ(2, device_enabled_count);
  EXPECT_EQ(2, device_disabled_count);
  EXPECT_EQ(3, cras_observer_notify_active_node_called);

  EXPECT_EQ(&d2_, cras_iodev_list_get_first_enabled_iodev(CRAS_STREAM_OUTPUT));
  EXPECT_EQ(&d1_, device_disabled_dev);

  EXPECT_EQ(2, audio_thread_add_stream_called);
  EXPECT_EQ(2, audio_thread_add_open_dev_called);
  EXPECT_EQ(1, audio_thread_rm_open_dev_called);

  EXPECT_EQ(
      0, cras_iodev_list_set_device_enabled_callback(NULL, NULL, NULL, NULL));
  cras_iodev_list_deinit();
}

TEST_F(IoDevTestSuite, UpdateActiveNode) {
  int rc;

  cras_iodev_list_init();

  d1_.direction = CRAS_STREAM_OUTPUT;
  rc = cras_iodev_list_add_output(&d1_);
  ASSERT_EQ(0, rc);

  d2_.direction = CRAS_STREAM_OUTPUT;
  rc = cras_iodev_list_add_output(&d2_);
  ASSERT_EQ(0, rc);

  cras_iodev_list_select_node(CRAS_STREAM_OUTPUT,
                              cras_make_node_id(d2_.info.idx, 1));

  EXPECT_EQ(2, update_active_node_called);
  EXPECT_EQ(&d2_, update_active_node_iodev_val[0]);
  EXPECT_EQ(1, update_active_node_node_idx_val[0]);
  EXPECT_EQ(1, update_active_node_dev_enabled_val[0]);

  /* Fake the active node idx on d2_, and later assert this node is
   * called for update_active_node when d2_ disabled. */
  d2_.active_node->idx = 2;
  cras_iodev_list_select_node(CRAS_STREAM_OUTPUT,
                              cras_make_node_id(d1_.info.idx, 0));

  EXPECT_EQ(5, update_active_node_called);
  EXPECT_EQ(&d2_, update_active_node_iodev_val[2]);
  EXPECT_EQ(&d1_, update_active_node_iodev_val[3]);
  EXPECT_EQ(2, update_active_node_node_idx_val[2]);
  EXPECT_EQ(0, update_active_node_node_idx_val[3]);
  EXPECT_EQ(0, update_active_node_dev_enabled_val[2]);
  EXPECT_EQ(1, update_active_node_dev_enabled_val[3]);
  EXPECT_EQ(2, cras_observer_notify_active_node_called);
  cras_iodev_list_deinit();
}

TEST_F(IoDevTestSuite, SelectNonExistingNode) {
  int rc;
  cras_iodev_list_init();

  d1_.direction = CRAS_STREAM_OUTPUT;
  rc = cras_iodev_list_add_output(&d1_);
  ASSERT_EQ(0, rc);

  cras_iodev_list_select_node(CRAS_STREAM_OUTPUT,
                              cras_make_node_id(d1_.info.idx, 0));
  EXPECT_EQ(1, d1_.is_enabled);

  /* Select non-existing node should disable all devices. */
  cras_iodev_list_select_node(CRAS_STREAM_OUTPUT, cras_make_node_id(2, 1));
  EXPECT_EQ(0, d1_.is_enabled);
  EXPECT_EQ(2, cras_observer_notify_active_node_called);
  cras_iodev_list_deinit();
}

// Devices with the wrong direction should be rejected.
TEST_F(IoDevTestSuite, AddWrongDirection) {
  int rc;

  rc = cras_iodev_list_add_input(&d1_);
  EXPECT_EQ(-EINVAL, rc);
  d1_.direction = CRAS_STREAM_INPUT;
  rc = cras_iodev_list_add_output(&d1_);
  EXPECT_EQ(-EINVAL, rc);
}

// Test adding/removing an iodev to the list.
TEST_F(IoDevTestSuite, AddRemoveOutput) {
  struct cras_iodev_info* dev_info;
  int rc;
  cras_iodev_list_init();

  rc = cras_iodev_list_add_output(&d1_);
  EXPECT_EQ(0, rc);
  // Test can't insert same iodev twice.
  rc = cras_iodev_list_add_output(&d1_);
  EXPECT_NE(0, rc);
  // Test insert a second output.
  rc = cras_iodev_list_add_output(&d2_);
  EXPECT_EQ(0, rc);

  // Test that it is removed.
  rc = cras_iodev_list_rm_output(&d1_);
  EXPECT_EQ(0, rc);
  // Test that we can't remove a dev twice.
  rc = cras_iodev_list_rm_output(&d1_);
  EXPECT_NE(0, rc);
  // Should be 1 dev now.
  rc = cras_iodev_list_get_outputs(&dev_info);
  EXPECT_EQ(1, rc);
  free(dev_info);
  // Passing null should return the number of outputs.
  rc = cras_iodev_list_get_outputs(NULL);
  EXPECT_EQ(1, rc);
  // Remove other dev.
  rc = cras_iodev_list_rm_output(&d2_);
  EXPECT_EQ(0, rc);
  // Should be 0 devs now.
  rc = cras_iodev_list_get_outputs(&dev_info);
  EXPECT_EQ(0, rc);
  free(dev_info);
  EXPECT_EQ(0, cras_observer_notify_active_node_called);
  cras_iodev_list_deinit();
}

// Test output_mute_changed callback.
TEST_F(IoDevTestSuite, OutputMuteChangedToMute) {
  cras_iodev_list_init();

  cras_iodev_list_add_output(&d1_);
  cras_iodev_list_add_output(&d2_);
  cras_iodev_list_add_output(&d3_);

  // d1_ and d2_ are enabled.
  cras_iodev_list_enable_dev(&d1_);
  cras_iodev_list_enable_dev(&d2_);

  // Assume d1 and d2 devices are open.
  d1_.state = CRAS_IODEV_STATE_OPEN;
  d2_.state = CRAS_IODEV_STATE_OPEN;
  d3_.state = CRAS_IODEV_STATE_CLOSE;

  // Execute the callback.
  observer_ops->output_mute_changed(NULL, 0, 1, 0);

  // d1_ and d2_ should set mute state through audio_thread_dev_start_ramp
  // because they are both open.
  EXPECT_EQ(2, audio_thread_dev_start_ramp_called);
  ASSERT_TRUE(
      dev_idx_in_vector(audio_thread_dev_start_ramp_dev_vector, d2_.info.idx));
  ASSERT_TRUE(
      dev_idx_in_vector(audio_thread_dev_start_ramp_dev_vector, d1_.info.idx));
  EXPECT_EQ(CRAS_IODEV_RAMP_REQUEST_DOWN_MUTE, audio_thread_dev_start_ramp_req);

  // d3_ should set mute state right away without calling ramp
  // because it is not open.
  EXPECT_EQ(1, set_mute_called);
  EXPECT_EQ(1, set_mute_dev_vector.size());
  ASSERT_TRUE(device_in_vector(set_mute_dev_vector, &d3_));

  cras_iodev_list_deinit();
}

// Test output_mute_changed callback.
TEST_F(IoDevTestSuite, OutputMuteChangedToUnmute) {
  cras_iodev_list_init();

  cras_iodev_list_add_output(&d1_);
  cras_iodev_list_add_output(&d2_);
  cras_iodev_list_add_output(&d3_);

  // d1_ and d2_ are enabled.
  cras_iodev_list_enable_dev(&d1_);
  cras_iodev_list_enable_dev(&d2_);

  // Assume d1 and d2 devices are open.
  d1_.state = CRAS_IODEV_STATE_OPEN;
  d2_.state = CRAS_IODEV_STATE_CLOSE;
  d3_.state = CRAS_IODEV_STATE_CLOSE;

  // Execute the callback.
  observer_ops->output_mute_changed(NULL, 0, 0, 0);

  // d1_ should set mute state through audio_thread_dev_start_ramp.
  EXPECT_EQ(1, audio_thread_dev_start_ramp_called);
  ASSERT_TRUE(
      dev_idx_in_vector(audio_thread_dev_start_ramp_dev_vector, d1_.info.idx));
  EXPECT_EQ(CRAS_IODEV_RAMP_REQUEST_UP_UNMUTE, audio_thread_dev_start_ramp_req);

  // d2_ and d3_ should set mute state right away because they both
  // are closed.
  EXPECT_EQ(2, set_mute_called);
  EXPECT_EQ(2, set_mute_dev_vector.size());
  ASSERT_TRUE(device_in_vector(set_mute_dev_vector, &d2_));
  ASSERT_TRUE(device_in_vector(set_mute_dev_vector, &d3_));

  cras_iodev_list_deinit();
}

// Test enable/disable an iodev.
TEST_F(IoDevTestSuite, EnableDisableDevice) {
  struct cras_rstream rstream;
  cras_iodev_list_init();
  device_enabled_count = 0;
  device_disabled_count = 0;
  memset(&rstream, 0, sizeof(rstream));

  EXPECT_EQ(0, cras_iodev_list_add_output(&d1_));

  EXPECT_EQ(0, cras_iodev_list_set_device_enabled_callback(
                   device_enabled_cb, device_disabled_cb, NULL, (void*)0xABCD));

  // Enable a device, fallback should be diabled accordingly.
  cras_iodev_list_enable_dev(&d1_);
  EXPECT_EQ(&d1_, device_enabled_dev);
  EXPECT_EQ((void*)0xABCD, device_enabled_cb_data);
  EXPECT_EQ(1, device_enabled_count);
  EXPECT_EQ(1, device_disabled_count);
  EXPECT_EQ(&d1_, cras_iodev_list_get_first_enabled_iodev(CRAS_STREAM_OUTPUT));

  // Connect a normal stream.
  cras_iodev_open_called = 0;
  stream_add_cb(&rstream);
  EXPECT_EQ(1, cras_iodev_open_called);

  stream_list_has_pinned_stream_ret[d1_.info.idx] = 0;
  // Disable a device. Expect dev is closed because there's no pinned stream.
  update_active_node_called = 0;
  cras_iodev_list_disable_dev(&d1_, false);
  EXPECT_EQ(&d1_, device_disabled_dev);
  EXPECT_EQ(2, device_disabled_count);
  EXPECT_EQ((void*)0xABCD, device_disabled_cb_data);

  EXPECT_EQ(1, audio_thread_rm_open_dev_called);
  EXPECT_EQ(1, cras_iodev_close_called);
  EXPECT_EQ(&d1_, cras_iodev_close_dev);
  EXPECT_EQ(1, update_active_node_called);

  EXPECT_EQ(0, cras_iodev_list_set_device_enabled_callback(
                   device_enabled_cb, device_disabled_cb, NULL, (void*)0xCDEF));
  EXPECT_EQ(2, cras_observer_notify_active_node_called);

  EXPECT_EQ(
      0, cras_iodev_list_set_device_enabled_callback(NULL, NULL, NULL, NULL));
  cras_iodev_list_deinit();
}

// Test adding/removing an input dev to the list.
TEST_F(IoDevTestSuite, AddRemoveInput) {
  struct cras_iodev_info* dev_info;
  int rc, i;
  uint64_t found_mask;

  d1_.direction = CRAS_STREAM_INPUT;
  d2_.direction = CRAS_STREAM_INPUT;

  cras_iodev_list_init();

  // Check no devices exist initially.
  rc = cras_iodev_list_get_inputs(NULL);
  EXPECT_EQ(0, rc);

  rc = cras_iodev_list_add_input(&d1_);
  EXPECT_EQ(0, rc);
  EXPECT_GE(d1_.info.idx, 0);
  // Test can't insert same iodev twice.
  rc = cras_iodev_list_add_input(&d1_);
  EXPECT_NE(0, rc);
  // Test insert a second input.
  rc = cras_iodev_list_add_input(&d2_);
  EXPECT_EQ(0, rc);
  EXPECT_GE(d2_.info.idx, 1);
  // make sure shared state was updated.
  EXPECT_EQ(2, server_state_stub.num_input_devs);
  EXPECT_EQ(d2_.info.idx, server_state_stub.input_devs[0].idx);
  EXPECT_EQ(d1_.info.idx, server_state_stub.input_devs[1].idx);

  // List the outputs.
  rc = cras_iodev_list_get_inputs(&dev_info);
  EXPECT_EQ(2, rc);
  if (rc == 2) {
    found_mask = 0;
    for (i = 0; i < rc; i++) {
      uint32_t idx = dev_info[i].idx;
      EXPECT_EQ(0, (found_mask & (static_cast<uint64_t>(1) << idx)));
      found_mask |= (static_cast<uint64_t>(1) << idx);
    }
  }
  if (rc > 0)
    free(dev_info);

  // Test that it is removed.
  rc = cras_iodev_list_rm_input(&d1_);
  EXPECT_EQ(0, rc);
  // Test that we can't remove a dev twice.
  rc = cras_iodev_list_rm_input(&d1_);
  EXPECT_NE(0, rc);
  // Should be 1 dev now.
  rc = cras_iodev_list_get_inputs(&dev_info);
  EXPECT_EQ(1, rc);
  free(dev_info);
  // Remove other dev.
  rc = cras_iodev_list_rm_input(&d2_);
  EXPECT_EQ(0, rc);
  // Shouldn't be any devices left.
  rc = cras_iodev_list_get_inputs(&dev_info);
  EXPECT_EQ(0, rc);
  free(dev_info);

  cras_iodev_list_deinit();
}

// Test adding/removing an input dev to the list without updating the server
// state.
TEST_F(IoDevTestSuite, AddRemoveInputNoSem) {
  int rc;

  d1_.direction = CRAS_STREAM_INPUT;
  d2_.direction = CRAS_STREAM_INPUT;

  server_state_update_begin_return = NULL;
  cras_iodev_list_init();

  rc = cras_iodev_list_add_input(&d1_);
  EXPECT_EQ(0, rc);
  EXPECT_GE(d1_.info.idx, 0);
  rc = cras_iodev_list_add_input(&d2_);
  EXPECT_EQ(0, rc);
  EXPECT_GE(d2_.info.idx, 1);

  EXPECT_EQ(0, cras_iodev_list_rm_input(&d1_));
  EXPECT_EQ(0, cras_iodev_list_rm_input(&d2_));
  cras_iodev_list_deinit();
}

// Test removing the last input.
TEST_F(IoDevTestSuite, RemoveLastInput) {
  struct cras_iodev_info* dev_info;
  int rc;

  d1_.direction = CRAS_STREAM_INPUT;
  d2_.direction = CRAS_STREAM_INPUT;

  cras_iodev_list_init();

  rc = cras_iodev_list_add_input(&d1_);
  EXPECT_EQ(0, rc);
  rc = cras_iodev_list_add_input(&d2_);
  EXPECT_EQ(0, rc);

  // Test that it is removed.
  rc = cras_iodev_list_rm_input(&d1_);
  EXPECT_EQ(0, rc);
  // Add it back.
  rc = cras_iodev_list_add_input(&d1_);
  EXPECT_EQ(0, rc);
  // And again.
  rc = cras_iodev_list_rm_input(&d1_);
  EXPECT_EQ(0, rc);
  // Add it back.
  rc = cras_iodev_list_add_input(&d1_);
  EXPECT_EQ(0, rc);
  // Remove other dev.
  rc = cras_iodev_list_rm_input(&d2_);
  EXPECT_EQ(0, rc);
  // Add it back.
  rc = cras_iodev_list_add_input(&d2_);
  EXPECT_EQ(0, rc);
  // Remove both.
  rc = cras_iodev_list_rm_input(&d2_);
  EXPECT_EQ(0, rc);
  rc = cras_iodev_list_rm_input(&d1_);
  EXPECT_EQ(0, rc);
  // Shouldn't be any devices left.
  rc = cras_iodev_list_get_inputs(&dev_info);
  EXPECT_EQ(0, rc);

  cras_iodev_list_deinit();
}

// Test nodes changed notification is sent.
TEST_F(IoDevTestSuite, NodesChangedNotification) {
  cras_iodev_list_init();
  EXPECT_EQ(1, cras_observer_add_called);

  cras_iodev_list_notify_nodes_changed();
  EXPECT_EQ(1, cras_observer_notify_nodes_called);

  cras_iodev_list_deinit();
  EXPECT_EQ(1, cras_observer_remove_called);
}

// Test callback function for left right swap mode is set and called.
TEST_F(IoDevTestSuite, NodesLeftRightSwappedCallback) {
  struct cras_iodev iodev;
  struct cras_ionode ionode;
  memset(&iodev, 0, sizeof(iodev));
  memset(&ionode, 0, sizeof(ionode));
  ionode.dev = &iodev;
  cras_iodev_list_notify_node_left_right_swapped(&ionode);
  EXPECT_EQ(1, cras_observer_notify_node_left_right_swapped_called);
}

// Test callback function for volume and gain are set and called.
TEST_F(IoDevTestSuite, VolumeGainCallback) {
  struct cras_iodev iodev;
  struct cras_ionode ionode;
  memset(&iodev, 0, sizeof(iodev));
  memset(&ionode, 0, sizeof(ionode));
  ionode.dev = &iodev;
  cras_iodev_list_notify_node_volume(&ionode);
  cras_iodev_list_notify_node_capture_gain(&ionode);
  EXPECT_EQ(1, cras_observer_notify_output_node_volume_called);
  EXPECT_EQ(1, cras_observer_notify_input_node_gain_called);
}

TEST_F(IoDevTestSuite, IodevListSetNodeAttr) {
  int rc;

  cras_iodev_list_init();

  // The list is empty now.
  rc = cras_iodev_list_set_node_attr(cras_make_node_id(0, 0),
                                     IONODE_ATTR_PLUGGED, 1);
  EXPECT_LE(rc, 0);
  EXPECT_EQ(0, set_node_plugged_called);

  // Add two device, each with one node.
  d1_.direction = CRAS_STREAM_INPUT;
  EXPECT_EQ(0, cras_iodev_list_add_input(&d1_));
  node1.idx = 1;
  EXPECT_EQ(0, cras_iodev_list_add_output(&d2_));
  node2.idx = 2;

  // Mismatch id
  rc = cras_iodev_list_set_node_attr(cras_make_node_id(d2_.info.idx, 1),
                                     IONODE_ATTR_PLUGGED, 1);
  EXPECT_LT(rc, 0);
  EXPECT_EQ(0, set_node_plugged_called);

  // Mismatch id
  rc = cras_iodev_list_set_node_attr(cras_make_node_id(d1_.info.idx, 2),
                                     IONODE_ATTR_PLUGGED, 1);
  EXPECT_LT(rc, 0);
  EXPECT_EQ(0, set_node_plugged_called);

  // Correct device id and node id
  rc = cras_iodev_list_set_node_attr(cras_make_node_id(d1_.info.idx, 1),
                                     IONODE_ATTR_PLUGGED, 1);
  EXPECT_EQ(rc, 0);
  EXPECT_EQ(1, set_node_plugged_called);
  cras_iodev_list_deinit();
}

TEST_F(IoDevTestSuite, SetNodeVolumeCaptureGain) {
  int rc;

  cras_iodev_list_init();

  d1_.direction = CRAS_STREAM_OUTPUT;
  rc = cras_iodev_list_add_output(&d1_);
  ASSERT_EQ(0, rc);
  node1.idx = 1;
  node1.dev = &d1_;

  // Do not ramp without software volume.
  d1_.software_volume_needed = 0;
  cras_iodev_list_set_node_attr(cras_make_node_id(d1_.info.idx, 1),
                                IONODE_ATTR_VOLUME, 10);
  EXPECT_EQ(1, cras_observer_notify_output_node_volume_called);
  EXPECT_EQ(0, cras_iodev_start_volume_ramp_called);

  // Even with software volume, device with NULL ramp won't trigger ramp start.
  d1_.software_volume_needed = 1;
  cras_iodev_list_set_node_attr(cras_make_node_id(d1_.info.idx, 1),
                                IONODE_ATTR_VOLUME, 20);
  EXPECT_EQ(2, cras_observer_notify_output_node_volume_called);
  EXPECT_EQ(0, cras_iodev_start_volume_ramp_called);

  // System mute prevents volume ramp from starting
  system_get_mute_return = true;
  cras_iodev_list_set_node_attr(cras_make_node_id(d1_.info.idx, 1),
                                IONODE_ATTR_VOLUME, 20);
  EXPECT_EQ(3, cras_observer_notify_output_node_volume_called);
  EXPECT_EQ(0, cras_iodev_start_volume_ramp_called);

  // Ramp starts only when it's non-NULL, software volume is used, and
  // system is not muted
  system_get_mute_return = false;
  d1_.ramp = reinterpret_cast<struct cras_ramp*>(0x1);
  cras_iodev_list_set_node_attr(cras_make_node_id(d1_.info.idx, 1),
                                IONODE_ATTR_VOLUME, 20);
  EXPECT_EQ(4, cras_observer_notify_output_node_volume_called);
  EXPECT_EQ(1, cras_iodev_start_volume_ramp_called);

  d1_.direction = CRAS_STREAM_INPUT;
  cras_iodev_list_set_node_attr(cras_make_node_id(d1_.info.idx, 1),
                                IONODE_ATTR_CAPTURE_GAIN, 15);
  EXPECT_EQ(1, cras_observer_notify_input_node_gain_called);
  cras_iodev_list_deinit();
}

TEST_F(IoDevTestSuite, SetNodeSwapLeftRight) {
  int rc;

  cras_iodev_list_init();

  rc = cras_iodev_list_add_output(&d1_);
  ASSERT_EQ(0, rc);
  node1.idx = 1;
  node1.dev = &d1_;

  cras_iodev_list_set_node_attr(cras_make_node_id(d1_.info.idx, 1),
                                IONODE_ATTR_SWAP_LEFT_RIGHT, 1);
  EXPECT_EQ(1, set_swap_mode_for_node_called);
  EXPECT_EQ(1, set_swap_mode_for_node_enable);
  EXPECT_EQ(1, node1.left_right_swapped);
  EXPECT_EQ(1, cras_observer_notify_node_left_right_swapped_called);

  cras_iodev_list_set_node_attr(cras_make_node_id(d1_.info.idx, 1),
                                IONODE_ATTR_SWAP_LEFT_RIGHT, 0);
  EXPECT_EQ(2, set_swap_mode_for_node_called);
  EXPECT_EQ(0, set_swap_mode_for_node_enable);
  EXPECT_EQ(0, node1.left_right_swapped);
  EXPECT_EQ(2, cras_observer_notify_node_left_right_swapped_called);
  cras_iodev_list_deinit();
}

TEST_F(IoDevTestSuite, SetNodeDisplayRotation) {
  int rc;
  cras_iodev_list_init();

  rc = cras_iodev_list_add_output(&d1_);
  ASSERT_EQ(0, rc);
  node1.idx = 1;
  node1.dev = &d1_;

  cras_iodev_list_set_node_attr(cras_make_node_id(d1_.info.idx, 1),
                                IONODE_ATTR_DISPLAY_ROTATION, ROTATE_180);
  EXPECT_EQ(1, set_display_rotation_for_node_called);
  EXPECT_EQ(ROTATE_180, display_rotation);
  EXPECT_EQ(ROTATE_180, node1.display_rotation);

  cras_iodev_list_set_node_attr(cras_make_node_id(d1_.info.idx, 1),
                                IONODE_ATTR_DISPLAY_ROTATION, ROTATE_270);
  EXPECT_EQ(2, set_display_rotation_for_node_called);
  EXPECT_EQ(ROTATE_270, display_rotation);
  EXPECT_EQ(ROTATE_270, node1.display_rotation);
  cras_iodev_list_deinit();
}

TEST_F(IoDevTestSuite, AddActiveNode) {
  int rc;
  struct cras_rstream rstream;

  memset(&rstream, 0, sizeof(rstream));

  cras_iodev_list_init();

  d1_.direction = CRAS_STREAM_OUTPUT;
  d2_.direction = CRAS_STREAM_OUTPUT;
  d3_.direction = CRAS_STREAM_OUTPUT;
  rc = cras_iodev_list_add_output(&d1_);
  ASSERT_EQ(0, rc);
  rc = cras_iodev_list_add_output(&d2_);
  ASSERT_EQ(0, rc);
  rc = cras_iodev_list_add_output(&d3_);
  ASSERT_EQ(0, rc);

  d1_.format = &fmt_;
  d2_.format = &fmt_;
  d3_.format = &fmt_;

  audio_thread_add_open_dev_called = 0;
  cras_iodev_list_add_active_node(CRAS_STREAM_OUTPUT,
                                  cras_make_node_id(d3_.info.idx, 1));
  ASSERT_EQ(audio_thread_add_open_dev_called, 0);
  ASSERT_EQ(audio_thread_rm_open_dev_called, 0);

  // If a stream is added, the device should be opened.
  stream_add_cb(&rstream);
  ASSERT_EQ(audio_thread_add_open_dev_called, 1);
  audio_thread_rm_open_dev_called = 0;
  audio_thread_drain_stream_return = 10;
  stream_rm_cb(&rstream);
  ASSERT_EQ(audio_thread_drain_stream_called, 1);
  ASSERT_EQ(audio_thread_rm_open_dev_called, 0);
  audio_thread_drain_stream_return = 0;
  clock_gettime_retspec.tv_sec = 15;
  clock_gettime_retspec.tv_nsec = 45;
  stream_rm_cb(&rstream);
  ASSERT_EQ(audio_thread_drain_stream_called, 2);
  ASSERT_EQ(0, audio_thread_rm_open_dev_called);
  // Stream should remain open for a while before being closed.
  // Test it is closed after 30 seconds.
  clock_gettime_retspec.tv_sec += 30;
  cras_tm_timer_cb(NULL, NULL);
  ASSERT_EQ(1, audio_thread_rm_open_dev_called);

  audio_thread_rm_open_dev_called = 0;
  cras_iodev_list_rm_output(&d3_);
  ASSERT_EQ(audio_thread_rm_open_dev_called, 0);

  /* Assert active devices was set to default one, when selected device
   * removed. */
  cras_iodev_list_rm_output(&d1_);
  cras_iodev_list_deinit();
}

TEST_F(IoDevTestSuite, OutputDevIdleClose) {
  int rc;
  struct cras_rstream rstream;

  memset(&rstream, 0, sizeof(rstream));
  cras_iodev_list_init();

  d1_.direction = CRAS_STREAM_OUTPUT;
  rc = cras_iodev_list_add_output(&d1_);
  EXPECT_EQ(0, rc);

  d1_.format = &fmt_;

  audio_thread_add_open_dev_called = 0;
  cras_iodev_list_add_active_node(CRAS_STREAM_OUTPUT,
                                  cras_make_node_id(d1_.info.idx, 1));
  EXPECT_EQ(0, audio_thread_add_open_dev_called);
  EXPECT_EQ(0, audio_thread_rm_open_dev_called);

  // If a stream is added, the device should be opened.
  stream_add_cb(&rstream);
  EXPECT_EQ(1, audio_thread_add_open_dev_called);

  audio_thread_rm_open_dev_called = 0;
  audio_thread_drain_stream_return = 0;
  clock_gettime_retspec.tv_sec = 15;
  stream_rm_cb(&rstream);
  EXPECT_EQ(1, audio_thread_drain_stream_called);
  EXPECT_EQ(0, audio_thread_rm_open_dev_called);
  EXPECT_EQ(1, cras_tm_create_timer_called);

  // Expect no rm dev happen because idle time not yet expire, and
  // new timer should be scheduled for the rest of the idle time.
  clock_gettime_retspec.tv_sec += 7;
  cras_tm_timer_cb(NULL, NULL);
  EXPECT_EQ(0, audio_thread_rm_open_dev_called);
  EXPECT_EQ(2, cras_tm_create_timer_called);

  // Expect d1_ be closed upon unplug, and the timer stay armed.
  cras_iodev_list_rm_output(&d1_);
  EXPECT_EQ(1, audio_thread_rm_open_dev_called);
  EXPECT_EQ(0, cras_tm_cancel_timer_called);

  // When timer eventually fired expect there's no more new
  // timer scheduled because d1_ has closed already.
  clock_gettime_retspec.tv_sec += 4;
  cras_tm_timer_cb(NULL, NULL);
  EXPECT_EQ(2, cras_tm_create_timer_called);
  cras_iodev_list_deinit();
}

TEST_F(IoDevTestSuite, DrainTimerCancel) {
  int rc;
  struct cras_rstream rstream;

  memset(&rstream, 0, sizeof(rstream));

  cras_iodev_list_init();

  d1_.direction = CRAS_STREAM_OUTPUT;
  rc = cras_iodev_list_add_output(&d1_);
  EXPECT_EQ(0, rc);

  d1_.format = &fmt_;

  audio_thread_add_open_dev_called = 0;
  cras_iodev_list_add_active_node(CRAS_STREAM_OUTPUT,
                                  cras_make_node_id(d1_.info.idx, 1));
  EXPECT_EQ(0, audio_thread_add_open_dev_called);
  EXPECT_EQ(0, audio_thread_rm_open_dev_called);

  // If a stream is added, the device should be opened.
  stream_add_cb(&rstream);
  EXPECT_EQ(1, audio_thread_add_open_dev_called);

  audio_thread_rm_open_dev_called = 0;
  audio_thread_drain_stream_return = 0;
  clock_gettime_retspec.tv_sec = 15;
  clock_gettime_retspec.tv_nsec = 45;
  stream_rm_cb(&rstream);
  EXPECT_EQ(1, audio_thread_drain_stream_called);
  EXPECT_EQ(0, audio_thread_rm_open_dev_called);

  // Add stream again, make sure device isn't closed after timeout.
  audio_thread_add_open_dev_called = 0;
  stream_add_cb(&rstream);
  EXPECT_EQ(0, audio_thread_add_open_dev_called);

  clock_gettime_retspec.tv_sec += 30;
  cras_tm_timer_cb(NULL, NULL);
  EXPECT_EQ(0, audio_thread_rm_open_dev_called);

  // Remove stream, and check the device is eventually closed.
  audio_thread_rm_open_dev_called = 0;
  audio_thread_drain_stream_called = 0;
  stream_rm_cb(&rstream);
  EXPECT_EQ(1, audio_thread_drain_stream_called);
  EXPECT_EQ(0, audio_thread_rm_open_dev_called);

  clock_gettime_retspec.tv_sec += 30;
  cras_tm_timer_cb(NULL, NULL);
  EXPECT_EQ(1, audio_thread_rm_open_dev_called);
  cras_iodev_list_deinit();
}

TEST_F(IoDevTestSuite, RemoveThenSelectActiveNode) {
  int rc;
  cras_node_id_t id;
  cras_iodev_list_init();

  d1_.direction = CRAS_STREAM_OUTPUT;
  d2_.direction = CRAS_STREAM_OUTPUT;

  /* d1_ will be the default_output */
  rc = cras_iodev_list_add_output(&d1_);
  ASSERT_EQ(0, rc);
  rc = cras_iodev_list_add_output(&d2_);
  ASSERT_EQ(0, rc);

  /* Test the scenario that the selected active output removed
   * from active dev list, should be able to select back again. */
  id = cras_make_node_id(d2_.info.idx, 1);

  cras_iodev_list_rm_active_node(CRAS_STREAM_OUTPUT, id);
  ASSERT_EQ(audio_thread_rm_open_dev_called, 0);

  cras_iodev_list_deinit();
}

TEST_F(IoDevTestSuite, CloseDevWithPinnedStream) {
  int rc;
  struct cras_rstream rstream1, rstream2;
  cras_iodev_list_init();

  d1_.direction = CRAS_STREAM_OUTPUT;
  d1_.info.idx = 1;
  rc = cras_iodev_list_add_output(&d1_);
  EXPECT_EQ(0, rc);

  memset(&rstream1, 0, sizeof(rstream1));
  memset(&rstream2, 0, sizeof(rstream2));
  rstream2.is_pinned = 1;
  rstream2.pinned_dev_idx = d1_.info.idx;

  d1_.format = &fmt_;
  audio_thread_add_open_dev_called = 0;
  EXPECT_EQ(0, audio_thread_add_open_dev_called);
  EXPECT_EQ(0, audio_thread_rm_open_dev_called);

  // Add a normal stream
  stream_add_cb(&rstream1);
  EXPECT_EQ(1, audio_thread_add_open_dev_called);

  // Add a pinned stream, expect another dev open call triggered.
  cras_iodev_open_called = 0;
  stream_add_cb(&rstream2);
  EXPECT_EQ(1, cras_iodev_open_called);

  // Force disable d1_ and make sure d1_ gets closed.
  audio_thread_rm_open_dev_called = 0;
  update_active_node_called = 0;
  cras_iodev_close_called = 0;
  cras_iodev_list_disable_dev(&d1_, 1);
  EXPECT_EQ(1, audio_thread_rm_open_dev_called);
  EXPECT_EQ(1, cras_iodev_close_called);
  EXPECT_EQ(&d1_, cras_iodev_close_dev);
  EXPECT_EQ(1, update_active_node_called);

  // Add back the two streams, one normal one pinned.
  audio_thread_add_open_dev_called = 0;
  audio_thread_rm_open_dev_called = 0;
  cras_iodev_open_called = 0;
  stream_add_cb(&rstream2);
  EXPECT_EQ(1, audio_thread_add_open_dev_called);
  EXPECT_EQ(1, cras_iodev_open_called);
  stream_add_cb(&rstream1);

  // Suspend d1_ and make sure d1_ gets closed.
  update_active_node_called = 0;
  cras_iodev_close_called = 0;
  cras_iodev_list_suspend_dev(d1_.info.idx);
  EXPECT_EQ(1, audio_thread_rm_open_dev_called);
  EXPECT_EQ(1, cras_iodev_close_called);
  EXPECT_EQ(&d1_, cras_iodev_close_dev);
  EXPECT_EQ(1, update_active_node_called);

  cras_iodev_list_resume_dev(d1_.info.idx);

  cras_iodev_list_deinit();
}

TEST_F(IoDevTestSuite, DisableDevWithPinnedStream) {
  int rc;
  struct cras_rstream rstream1;
  cras_iodev_list_init();

  d1_.direction = CRAS_STREAM_OUTPUT;
  rc = cras_iodev_list_add_output(&d1_);
  EXPECT_EQ(0, rc);

  memset(&rstream1, 0, sizeof(rstream1));
  rstream1.is_pinned = 1;
  rstream1.pinned_dev_idx = d1_.info.idx;

  d1_.format = &fmt_;
  audio_thread_add_open_dev_called = 0;
  cras_iodev_list_add_active_node(CRAS_STREAM_OUTPUT,
                                  cras_make_node_id(d1_.info.idx, 1));
  EXPECT_EQ(0, audio_thread_add_open_dev_called);
  EXPECT_EQ(0, audio_thread_rm_open_dev_called);

  // Add a pinned stream.
  cras_iodev_open_called = 0;
  stream_add_cb(&rstream1);
  EXPECT_EQ(1, audio_thread_add_open_dev_called);
  EXPECT_EQ(1, cras_iodev_open_called);

  // Disable d1_ expect no close dev triggered because pinned stream.
  stream_list_has_pinned_stream_ret[d1_.info.idx] = 1;
  audio_thread_rm_open_dev_called = 0;
  update_active_node_called = 0;
  cras_iodev_close_called = 0;
  cras_iodev_list_disable_dev(&d1_, 0);
  EXPECT_EQ(0, audio_thread_rm_open_dev_called);
  EXPECT_EQ(0, cras_iodev_close_called);
  EXPECT_EQ(0, update_active_node_called);

  cras_iodev_list_deinit();
}

TEST_F(IoDevTestSuite, AddRemovePinnedStream) {
  struct cras_rstream rstream;

  cras_iodev_list_init();

  // Add 2 output devices.
  d1_.direction = CRAS_STREAM_OUTPUT;
  d1_.info.idx = 1;
  EXPECT_EQ(0, cras_iodev_list_add_output(&d1_));
  cras_iodev_list_select_node(CRAS_STREAM_OUTPUT,
                              cras_make_node_id(d1_.info.idx, 0));
  EXPECT_EQ(2, update_active_node_called);
  EXPECT_EQ(&d1_, update_active_node_iodev_val[0]);

  d2_.direction = CRAS_STREAM_OUTPUT;
  d2_.info.idx = 2;
  EXPECT_EQ(0, cras_iodev_list_add_output(&d2_));

  d1_.format = &fmt_;
  d2_.format = &fmt_;

  // Setup pinned stream.
  memset(&rstream, 0, sizeof(rstream));
  rstream.is_pinned = 1;
  rstream.pinned_dev_idx = d1_.info.idx;

  // Add pinned stream to d1.
  update_active_node_called = 0;
  EXPECT_EQ(0, stream_add_cb(&rstream));
  EXPECT_EQ(1, audio_thread_add_stream_called);
  EXPECT_EQ(&d1_, audio_thread_add_stream_dev);
  EXPECT_EQ(&rstream, audio_thread_add_stream_stream);
  EXPECT_EQ(1, update_active_node_called);
  // Init d1_ because of pinned stream
  EXPECT_EQ(&d1_, update_active_node_iodev_val[0]);

  // Select d2, check pinned stream is not added to d2.
  update_active_node_called = 0;
  stream_list_has_pinned_stream_ret[d1_.info.idx] = 1;
  cras_iodev_list_select_node(CRAS_STREAM_OUTPUT,
                              cras_make_node_id(d2_.info.idx, 0));
  EXPECT_EQ(1, audio_thread_add_stream_called);
  EXPECT_EQ(2, update_active_node_called);
  // Unselect d1_ and select to d2_
  EXPECT_EQ(&d2_, update_active_node_iodev_val[0]);
  EXPECT_EQ(&mock_empty_iodev[CRAS_STREAM_OUTPUT],
            update_active_node_iodev_val[1]);

  // Remove pinned stream from d1, check d1 is closed after stream removed.
  update_active_node_called = 0;
  stream_list_has_pinned_stream_ret[d1_.info.idx] = 0;
  EXPECT_EQ(0, stream_rm_cb(&rstream));
  EXPECT_EQ(1, cras_iodev_close_called);
  EXPECT_EQ(&d1_, cras_iodev_close_dev);
  EXPECT_EQ(1, update_active_node_called);
  // close pinned device
  EXPECT_EQ(&d1_, update_active_node_iodev_val[0]);

  // Assume dev is already opened, add pin stream should not trigger another
  // update_active_node call, but will trigger audio_thread_add_stream.
  audio_thread_is_dev_open_ret = 1;
  update_active_node_called = 0;
  EXPECT_EQ(0, stream_add_cb(&rstream));
  EXPECT_EQ(0, update_active_node_called);
  EXPECT_EQ(2, audio_thread_add_stream_called);

  cras_iodev_list_deinit();
}

TEST_F(IoDevTestSuite, SuspendResumePinnedStream) {
  struct cras_rstream rstream;

  cras_iodev_list_init();

  // Add 2 output devices.
  d1_.direction = CRAS_STREAM_OUTPUT;
  EXPECT_EQ(0, cras_iodev_list_add_output(&d1_));
  d2_.direction = CRAS_STREAM_OUTPUT;
  EXPECT_EQ(0, cras_iodev_list_add_output(&d2_));

  d1_.format = &fmt_;
  d2_.format = &fmt_;

  // Setup pinned stream.
  memset(&rstream, 0, sizeof(rstream));
  rstream.is_pinned = 1;
  rstream.pinned_dev_idx = d1_.info.idx;

  // Add pinned stream to d1.
  EXPECT_EQ(0, stream_add_cb(&rstream));
  EXPECT_EQ(1, audio_thread_add_stream_called);
  EXPECT_EQ(&d1_, audio_thread_add_stream_dev);
  EXPECT_EQ(&rstream, audio_thread_add_stream_stream);

  DL_APPEND(stream_list_get_ret, &rstream);

  // Test for suspend

  // Device state enters no_stream after stream is disconnected.
  d1_.state = CRAS_IODEV_STATE_NO_STREAM_RUN;
  // Device has no pinned stream now. But this pinned stream remains in
  // stream_list.
  stream_list_has_pinned_stream_ret[d1_.info.idx] = 0;

  // Suspend
  observer_ops->suspend_changed(NULL, 1);

  // Verify that stream is disconnected and d1 is closed.
  EXPECT_EQ(1, audio_thread_disconnect_stream_called);
  EXPECT_EQ(&rstream, audio_thread_disconnect_stream_stream);
  EXPECT_EQ(1, cras_iodev_close_called);
  EXPECT_EQ(&d1_, cras_iodev_close_dev);

  // Test for resume
  cras_iodev_open_called = 0;
  audio_thread_add_stream_called = 0;
  audio_thread_add_stream_stream = NULL;
  d1_.state = CRAS_IODEV_STATE_CLOSE;

  // Resume
  observer_ops->suspend_changed(NULL, 0);

  // Verify that device is opened and stream is attached to the device.
  EXPECT_EQ(1, cras_iodev_open_called);
  EXPECT_EQ(1, audio_thread_add_stream_called);
  EXPECT_EQ(&rstream, audio_thread_add_stream_stream);
  cras_iodev_list_deinit();
}

TEST_F(IoDevTestSuite, HotwordStreamsAddedThenSuspendResume) {
  struct cras_rstream rstream;
  struct cras_rstream* stream_list = NULL;
  cras_iodev_list_init();

  node1.type = CRAS_NODE_TYPE_HOTWORD;
  d1_.direction = CRAS_STREAM_INPUT;
  EXPECT_EQ(0, cras_iodev_list_add_input(&d1_));

  d1_.format = &fmt_;

  memset(&rstream, 0, sizeof(rstream));
  rstream.is_pinned = 1;
  rstream.pinned_dev_idx = d1_.info.idx;
  rstream.flags = HOTWORD_STREAM;

  /* Add a hotword stream. */
  EXPECT_EQ(0, stream_add_cb(&rstream));
  EXPECT_EQ(1, audio_thread_add_stream_called);
  EXPECT_EQ(&d1_, audio_thread_add_stream_dev);
  EXPECT_EQ(&rstream, audio_thread_add_stream_stream);

  DL_APPEND(stream_list, &rstream);
  stream_list_get_ret = stream_list;

  /* Suspend hotword streams, verify the existing stream disconnects
   * from the hotword device and connects to the empty iodev. */
  EXPECT_EQ(0, cras_iodev_list_suspend_hotword_streams());
  EXPECT_EQ(1, audio_thread_disconnect_stream_called);
  EXPECT_EQ(&rstream, audio_thread_disconnect_stream_stream);
  EXPECT_EQ(&d1_, audio_thread_disconnect_stream_dev);
  EXPECT_EQ(2, audio_thread_add_stream_called);
  EXPECT_EQ(&rstream, audio_thread_add_stream_stream);
  EXPECT_EQ(&mock_hotword_iodev, audio_thread_add_stream_dev);

  /* Resume hotword streams, verify the stream disconnects from
   * the empty iodev and connects back to the real hotword iodev. */
  EXPECT_EQ(0, cras_iodev_list_resume_hotword_stream());
  EXPECT_EQ(2, audio_thread_disconnect_stream_called);
  EXPECT_EQ(&rstream, audio_thread_disconnect_stream_stream);
  EXPECT_EQ(&mock_hotword_iodev, audio_thread_disconnect_stream_dev);
  EXPECT_EQ(3, audio_thread_add_stream_called);
  EXPECT_EQ(&rstream, audio_thread_add_stream_stream);
  EXPECT_EQ(&d1_, audio_thread_add_stream_dev);
  cras_iodev_list_deinit();
}

TEST_F(IoDevTestSuite, HotwordStreamsAddedAfterSuspend) {
  struct cras_rstream rstream;
  struct cras_rstream* stream_list = NULL;
  cras_iodev_list_init();

  node1.type = CRAS_NODE_TYPE_HOTWORD;
  d1_.direction = CRAS_STREAM_INPUT;
  EXPECT_EQ(0, cras_iodev_list_add_input(&d1_));

  d1_.format = &fmt_;

  memset(&rstream, 0, sizeof(rstream));
  rstream.is_pinned = 1;
  rstream.pinned_dev_idx = d1_.info.idx;
  rstream.flags = HOTWORD_STREAM;

  /* Suspends hotword streams before a stream connected. */
  EXPECT_EQ(0, cras_iodev_list_suspend_hotword_streams());
  EXPECT_EQ(0, audio_thread_disconnect_stream_called);
  EXPECT_EQ(0, audio_thread_add_stream_called);

  DL_APPEND(stream_list, &rstream);
  stream_list_get_ret = stream_list;

  /* Hotword stream connected, verify it is added to the empty iodev. */
  EXPECT_EQ(0, stream_add_cb(&rstream));
  EXPECT_EQ(1, audio_thread_add_stream_called);
  EXPECT_EQ(&mock_hotword_iodev, audio_thread_add_stream_dev);
  EXPECT_EQ(&rstream, audio_thread_add_stream_stream);

  /* Resume hotword streams, now the existing hotword stream should disconnect
   * from the empty iodev and connect to the real hotword iodev. */
  EXPECT_EQ(0, cras_iodev_list_resume_hotword_stream());
  EXPECT_EQ(1, audio_thread_disconnect_stream_called);
  EXPECT_EQ(&rstream, audio_thread_disconnect_stream_stream);
  EXPECT_EQ(&mock_hotword_iodev, audio_thread_disconnect_stream_dev);
  EXPECT_EQ(2, audio_thread_add_stream_called);
  EXPECT_EQ(&rstream, audio_thread_add_stream_stream);
  EXPECT_EQ(&d1_, audio_thread_add_stream_dev);
  cras_iodev_list_deinit();
}

TEST_F(IoDevTestSuite, GetSCOPCMIodevs) {
  cras_iodev_list_init();

  fake_sco_in_dev.direction = CRAS_STREAM_INPUT;
  fake_sco_in_node.btflags |= CRAS_BT_FLAG_SCO_OFFLOAD;
  cras_iodev_list_add_input(&fake_sco_in_dev);
  fake_sco_out_dev.direction = CRAS_STREAM_OUTPUT;
  fake_sco_out_node.btflags |= CRAS_BT_FLAG_SCO_OFFLOAD;
  cras_iodev_list_add_output(&fake_sco_out_dev);

  EXPECT_EQ(&fake_sco_in_dev,
            cras_iodev_list_get_sco_pcm_iodev(CRAS_STREAM_INPUT));
  EXPECT_EQ(&fake_sco_out_dev,
            cras_iodev_list_get_sco_pcm_iodev(CRAS_STREAM_OUTPUT));

  cras_iodev_list_deinit();
}

TEST_F(IoDevTestSuite, HotwordStreamsPausedAtSystemSuspend) {
  struct cras_rstream rstream;
  struct cras_rstream* stream_list = NULL;
  cras_iodev_list_init();

  node1.type = CRAS_NODE_TYPE_HOTWORD;
  d1_.direction = CRAS_STREAM_INPUT;
  EXPECT_EQ(0, cras_iodev_list_add_input(&d1_));

  d1_.format = &fmt_;

  memset(&rstream, 0, sizeof(rstream));
  rstream.is_pinned = 1;
  rstream.pinned_dev_idx = d1_.info.idx;
  rstream.flags = HOTWORD_STREAM;

  /* Add a hotword stream. */
  EXPECT_EQ(0, stream_add_cb(&rstream));
  EXPECT_EQ(1, audio_thread_add_stream_called);
  EXPECT_EQ(&d1_, audio_thread_add_stream_dev);
  EXPECT_EQ(&rstream, audio_thread_add_stream_stream);

  DL_APPEND(stream_list, &rstream);
  stream_list_get_ret = stream_list;

  server_state_hotword_pause_at_suspend = 1;

  /* Trigger system suspend. Verify hotword stream is moved to empty dev. */
  observer_ops->suspend_changed(NULL, 1);
  EXPECT_EQ(1, audio_thread_disconnect_stream_called);
  EXPECT_EQ(&rstream, audio_thread_disconnect_stream_stream);
  EXPECT_EQ(&d1_, audio_thread_disconnect_stream_dev);
  EXPECT_EQ(2, audio_thread_add_stream_called);
  EXPECT_EQ(&rstream, audio_thread_add_stream_stream);
  EXPECT_EQ(&mock_hotword_iodev, audio_thread_add_stream_dev);

  /* Trigger system resume. Verify hotword stream is moved to real dev.*/
  observer_ops->suspend_changed(NULL, 0);
  EXPECT_EQ(2, audio_thread_disconnect_stream_called);
  EXPECT_EQ(&rstream, audio_thread_disconnect_stream_stream);
  EXPECT_EQ(&mock_hotword_iodev, audio_thread_disconnect_stream_dev);
  EXPECT_EQ(3, audio_thread_add_stream_called);
  EXPECT_EQ(&rstream, audio_thread_add_stream_stream);
  EXPECT_EQ(&d1_, audio_thread_add_stream_dev);

  server_state_hotword_pause_at_suspend = 0;
  audio_thread_disconnect_stream_called = 0;
  audio_thread_add_stream_called = 0;

  /* Trigger system suspend. Verify hotword stream is not touched. */
  observer_ops->suspend_changed(NULL, 1);
  EXPECT_EQ(0, audio_thread_disconnect_stream_called);
  EXPECT_EQ(0, audio_thread_add_stream_called);

  /* Trigger system resume. Verify hotword stream is not touched.*/
  observer_ops->suspend_changed(NULL, 0);
  EXPECT_EQ(0, audio_thread_disconnect_stream_called);
  EXPECT_EQ(0, audio_thread_add_stream_called);

  cras_iodev_list_deinit();
}

TEST_F(IoDevTestSuite, SetNoiseCancellation) {
  struct cras_rstream rstream;
  struct cras_rstream* stream_list = NULL;
  int rc;

  memset(&rstream, 0, sizeof(rstream));

  cras_iodev_list_init();

  d1_.direction = CRAS_STREAM_INPUT;
  rc = cras_iodev_list_add_input(&d1_);
  ASSERT_EQ(0, rc);

  d1_.format = &fmt_;

  rstream.direction = CRAS_STREAM_INPUT;

  audio_thread_add_open_dev_called = 0;
  audio_thread_rm_open_dev_called = 0;
  cras_iodev_list_add_active_node(CRAS_STREAM_INPUT,
                                  cras_make_node_id(d1_.info.idx, 1));
  DL_APPEND(stream_list, &rstream);
  stream_add_cb(&rstream);
  stream_list_get_ret = stream_list;
  EXPECT_EQ(1, audio_thread_add_stream_called);
  EXPECT_EQ(1, audio_thread_add_open_dev_called);

  // reset_for_noise_cancellation causes device suspend & resume
  // While suspending d1_: rm d1_, open fallback
  // While resuming d1_: rm fallback, open d1_
  cras_iodev_list_reset_for_noise_cancellation();
  EXPECT_EQ(3, audio_thread_add_open_dev_called);
  EXPECT_EQ(2, audio_thread_rm_open_dev_called);

  cras_iodev_list_deinit();
}

TEST_F(IoDevTestSuite, BlockNoiseCancellationByActiveSpeaker) {
  uint32_t default_audio_effect = 0x8000;

  cras_iodev_list_init();

  d1_.direction = CRAS_STREAM_INPUT;
  node1.audio_effect = default_audio_effect | EFFECT_TYPE_NOISE_CANCELLATION;
  d2_.direction = CRAS_STREAM_INPUT;
  node2.audio_effect = default_audio_effect;

  d3_.direction = CRAS_STREAM_OUTPUT;
  node3.type = CRAS_NODE_TYPE_INTERNAL_SPEAKER;

  // Check no devices exist initially.
  EXPECT_EQ(0, cras_iodev_list_get_inputs(NULL));

  EXPECT_EQ(0, cras_iodev_list_add_input(&d1_));
  EXPECT_GE(d1_.info.idx, 0);
  EXPECT_EQ(0, cras_iodev_list_add_input(&d2_));
  EXPECT_GE(d2_.info.idx, 1);
  EXPECT_EQ(0, cras_iodev_list_add_output(&d3_));
  EXPECT_GE(d3_.info.idx, 2);

  // Make sure shared state was updated.
  EXPECT_EQ(2, server_state_stub.num_input_devs);
  ASSERT_EQ(2, server_state_stub.num_input_nodes);
  EXPECT_EQ(node2.audio_effect, server_state_stub.input_nodes[0].audio_effect);
  EXPECT_EQ(node1.audio_effect, server_state_stub.input_nodes[1].audio_effect);
  EXPECT_EQ(0, cras_observer_notify_nodes_called);

  // Block Noise Cancallation in audio_effect.
  cras_iodev_list_enable_dev(&d3_);
  ASSERT_EQ(2, server_state_stub.num_input_nodes);
  EXPECT_EQ(default_audio_effect,
            server_state_stub.input_nodes[0].audio_effect);
  EXPECT_EQ(default_audio_effect,
            server_state_stub.input_nodes[1].audio_effect);
  EXPECT_EQ(1, cras_observer_notify_nodes_called);

  // Unblock Noise Cancallation in audio_effect.
  cras_iodev_list_disable_dev(&d3_, false);
  ASSERT_EQ(2, server_state_stub.num_input_nodes);
  EXPECT_EQ(node2.audio_effect, server_state_stub.input_nodes[0].audio_effect);
  EXPECT_EQ(node1.audio_effect, server_state_stub.input_nodes[1].audio_effect);
  EXPECT_EQ(2, cras_observer_notify_nodes_called);

  cras_iodev_list_deinit();
}

TEST_F(IoDevTestSuite, BlockNoiseCancellationByPinnedSpeaker) {
  struct cras_rstream rstream1, rstream2;
  uint32_t default_audio_effect = 0x8000;

  cras_iodev_list_init();

  // Add 2 output devices.
  d1_.direction = CRAS_STREAM_OUTPUT;
  d1_.info.idx = 1;
  node1.idx = 1;
  node1.type = CRAS_NODE_TYPE_INTERNAL_SPEAKER;
  EXPECT_EQ(0, cras_iodev_list_add_output(&d1_));

  d2_.direction = CRAS_STREAM_OUTPUT;
  d2_.info.idx = 2;
  node2.idx = 2;
  node2.type = CRAS_NODE_TYPE_HEADPHONE;
  EXPECT_EQ(0, cras_iodev_list_add_output(&d2_));

  // Add 1 input device for checking Noise Cancellation state.
  d3_.direction = CRAS_STREAM_INPUT;
  node3.audio_effect = default_audio_effect | EFFECT_TYPE_NOISE_CANCELLATION;
  EXPECT_EQ(0, cras_iodev_list_add_input(&d3_));

  // Make sure shared state was updated.
  EXPECT_EQ(1, server_state_stub.num_input_devs);
  ASSERT_EQ(1, server_state_stub.num_input_nodes);
  EXPECT_EQ(node3.audio_effect, server_state_stub.input_nodes[0].audio_effect);
  EXPECT_EQ(0, cras_observer_notify_nodes_called);

  // Select internal speaker as the active node.
  cras_iodev_list_select_node(CRAS_STREAM_OUTPUT,
                              cras_make_node_id(d1_.info.idx, node1.idx));

  // Block Noise Cancellation because internal speaker is enabled.
  ASSERT_EQ(1, server_state_stub.num_input_nodes);
  EXPECT_EQ(default_audio_effect,
            server_state_stub.input_nodes[0].audio_effect);
  EXPECT_EQ(1, cras_observer_notify_nodes_called);

  d1_.format = &fmt_;
  d2_.format = &fmt_;

  // Setup pinned streams.
  memset(&rstream1, 0, sizeof(rstream1));
  rstream1.is_pinned = 1;
  rstream1.pinned_dev_idx = d1_.info.idx;
  memset(&rstream2, 0, sizeof(rstream2));
  rstream2.is_pinned = 1;
  rstream2.pinned_dev_idx = d2_.info.idx;

  // Add pinned stream to d1 (internal speaker).
  update_active_node_called = 0;
  EXPECT_EQ(0, stream_add_cb(&rstream1));
  EXPECT_EQ(1, audio_thread_add_stream_called);
  EXPECT_EQ(&d1_, audio_thread_add_stream_dev);
  EXPECT_EQ(&rstream1, audio_thread_add_stream_stream);
  EXPECT_EQ(1, update_active_node_called);
  EXPECT_EQ(&d1_, update_active_node_iodev_val[0]);
  // Noise Cancellation is still blocked.
  ASSERT_EQ(1, server_state_stub.num_input_nodes);
  EXPECT_EQ(default_audio_effect,
            server_state_stub.input_nodes[0].audio_effect);
  EXPECT_EQ(1, cras_observer_notify_nodes_called);

  // Add pinned stream to d2 (headphone).
  EXPECT_EQ(0, stream_add_cb(&rstream2));
  EXPECT_EQ(2, audio_thread_add_stream_called);
  EXPECT_EQ(&d2_, audio_thread_add_stream_dev);
  EXPECT_EQ(&rstream2, audio_thread_add_stream_stream);
  EXPECT_EQ(2, update_active_node_called);
  EXPECT_EQ(&d2_, update_active_node_iodev_val[1]);
  // Nothing changed for adding pinned stream to d2.
  ASSERT_EQ(1, server_state_stub.num_input_nodes);
  EXPECT_EQ(default_audio_effect,
            server_state_stub.input_nodes[0].audio_effect);
  EXPECT_EQ(1, cras_observer_notify_nodes_called);

  // Remove pinned stream from d2.
  stream_list_has_pinned_stream_ret[d2_.info.idx] = 0;
  EXPECT_EQ(0, stream_rm_cb(&rstream2));
  // Nothing changed for removing pinned stream from d2.
  ASSERT_EQ(1, server_state_stub.num_input_nodes);
  EXPECT_EQ(default_audio_effect,
            server_state_stub.input_nodes[0].audio_effect);
  EXPECT_EQ(1, cras_observer_notify_nodes_called);

  stream_list_has_pinned_stream_ret[d1_.info.idx] = 1;
  // Select headphone as the active node.
  cras_iodev_list_select_node(CRAS_STREAM_OUTPUT,
                              cras_make_node_id(d2_.info.idx, node2.idx));
  // Noise Cancellation is still blocked because pinned stream is still attached
  // to d1 (internal speaker).
  ASSERT_EQ(1, server_state_stub.num_input_nodes);
  EXPECT_EQ(default_audio_effect,
            server_state_stub.input_nodes[0].audio_effect);
  EXPECT_EQ(1, cras_observer_notify_nodes_called);

  // Remove pinned stream from d1.
  stream_list_has_pinned_stream_ret[d1_.info.idx] = 0;
  EXPECT_EQ(0, stream_rm_cb(&rstream1));
  // Unblock Noise Cancellation because pinned stream is removed from d1.
  ASSERT_EQ(1, server_state_stub.num_input_nodes);
  EXPECT_EQ(node3.audio_effect, server_state_stub.input_nodes[0].audio_effect);
  EXPECT_EQ(2, cras_observer_notify_nodes_called);

  cras_iodev_list_deinit();
}

TEST_F(IoDevTestSuite, SetAecRefReconnectStream) {
  struct cras_rstream rstream;
  struct cras_rstream* stream_list = NULL;
  int rc;

  memset(&rstream, 0, sizeof(rstream));

  cras_iodev_list_init();

  d1_.direction = CRAS_STREAM_OUTPUT;
  rc = cras_iodev_list_add_output(&d1_);
  ASSERT_EQ(0, rc);

  /* Prepare an enabled input iodev for stream to attach to. */
  d2_.direction = CRAS_STREAM_INPUT;
  d2_.info.idx = 2;
  EXPECT_EQ(0, cras_iodev_list_add_input(&d2_));
  cras_iodev_list_select_node(CRAS_STREAM_INPUT,
                              cras_make_node_id(d2_.info.idx, 0));

  rstream.direction = CRAS_STREAM_INPUT;
  rstream.stream_id = 123;
  rstream.stream_apm = reinterpret_cast<struct cras_stream_apm*>(0x987);

  DL_APPEND(stream_list, &rstream);
  stream_add_cb(&rstream);
  stream_list_get_ret = stream_list;

  audio_thread_add_stream_called = 0;
  audio_thread_disconnect_stream_called = 0;
  cras_stream_apm_set_aec_ref_called = 0;
  cras_stream_apm_remove_called = 0;
  cras_stream_apm_add_called = 0;
  cras_iodev_list_set_aec_ref(123, d1_.info.idx);
  EXPECT_EQ(1, cras_stream_apm_set_aec_ref_called);
  /* Verify the stream and apm went through correct life cycles. Because
   * setting AEC ref is expected to trigger stream reconnection. */
  EXPECT_EQ(1, audio_thread_disconnect_stream_called);
  EXPECT_EQ(1, cras_stream_apm_remove_called);
  EXPECT_EQ(1, cras_stream_apm_add_called);
  EXPECT_EQ(1, audio_thread_add_stream_called);

  cras_iodev_list_deinit();
}

TEST_F(IoDevTestSuite, ReconnectStreamsWithApm) {
  struct cras_rstream rstream;
  struct cras_rstream* stream_list = NULL;
  int rc;

  memset(&rstream, 0, sizeof(rstream));
  cras_iodev_list_init();

  d1_.direction = CRAS_STREAM_INPUT;
  rc = cras_iodev_list_add_input(&d1_);
  ASSERT_EQ(0, rc);

  /* Prepare an enabled input iodev for stream to attach to. */
  d2_.direction = CRAS_STREAM_INPUT;
  d2_.info.idx = 2;
  EXPECT_EQ(0, cras_iodev_list_add_input(&d2_));
  cras_iodev_list_select_node(CRAS_STREAM_INPUT,
                              cras_make_node_id(d2_.info.idx, 0));

  rstream.direction = CRAS_STREAM_INPUT;
  rstream.stream_apm = reinterpret_cast<struct cras_stream_apm*>(0x987);

  DL_APPEND(stream_list, &rstream);
  stream_add_cb(&rstream);
  stream_list_get_ret = stream_list;

  audio_thread_add_stream_called = 0;
  audio_thread_disconnect_stream_called = 0;
  cras_stream_apm_remove_called = 0;
  cras_stream_apm_add_called = 0;
  cras_iodev_list_reconnect_streams_with_apm();
  /* Verify the stream and apm to through correct life cycles. */
  EXPECT_EQ(1, audio_thread_disconnect_stream_called);
  EXPECT_EQ(1, cras_stream_apm_remove_called);
  EXPECT_EQ(1, cras_stream_apm_add_called);
  EXPECT_EQ(1, audio_thread_add_stream_called);

  cras_iodev_list_deinit();
}

TEST_F(IoDevTestSuite, BlockNoiseCancellationInHybridCases) {
  struct cras_rstream rstream;
  uint32_t default_audio_effect = 0x8000;

  cras_iodev_list_init();

  // Add output device.
  d1_.direction = CRAS_STREAM_OUTPUT;
  d1_.info.idx = 1;
  node1.idx = 1;
  node1.type = CRAS_NODE_TYPE_INTERNAL_SPEAKER;
  EXPECT_EQ(0, cras_iodev_list_add_output(&d1_));

  // Add input device for checking Noise Cancellation state.
  d2_.direction = CRAS_STREAM_INPUT;
  d2_.info.idx = 2;
  node2.idx = 2;
  node2.audio_effect = default_audio_effect | EFFECT_TYPE_NOISE_CANCELLATION;
  EXPECT_EQ(0, cras_iodev_list_add_input(&d2_));

  // Make sure shared state was updated.
  ASSERT_EQ(1, server_state_stub.num_input_nodes);
  EXPECT_EQ(node2.audio_effect, server_state_stub.input_nodes[0].audio_effect);
  EXPECT_EQ(0, cras_observer_notify_nodes_called);

  // Select internal speaker as the active node, which means internal speaker
  // device is enabled, yet opened.
  cras_iodev_list_select_node(CRAS_STREAM_OUTPUT,
                              cras_make_node_id(d1_.info.idx, node1.idx));
  EXPECT_EQ(1, cras_iodev_list_dev_is_enabled(&d1_));

  // Noise Cancellation is blocked.
  ASSERT_EQ(1, server_state_stub.num_input_nodes);
  EXPECT_EQ(default_audio_effect,
            server_state_stub.input_nodes[0].audio_effect);
  EXPECT_EQ(1, cras_observer_notify_nodes_called);

  d1_.format = &fmt_;

  // Setup pinned streams.
  memset(&rstream, 0, sizeof(rstream));
  rstream.is_pinned = 1;
  rstream.pinned_dev_idx = d1_.info.idx;

  // Add pinned stream to d1, which means internal speaker device is opened.
  update_active_node_called = 0;
  EXPECT_EQ(0, stream_add_cb(&rstream));
  EXPECT_EQ(1, audio_thread_add_stream_called);
  EXPECT_EQ(&d1_, audio_thread_add_stream_dev);
  EXPECT_EQ(&rstream, audio_thread_add_stream_stream);
  EXPECT_EQ(1, update_active_node_called);
  EXPECT_EQ(&d1_, update_active_node_iodev_val[0]);
  EXPECT_EQ(1, cras_iodev_is_open(&d1_));

  // Noise Cancellation is still blocked. notify_nodes shouldn't be called.
  ASSERT_EQ(1, server_state_stub.num_input_nodes);
  EXPECT_EQ(default_audio_effect,
            server_state_stub.input_nodes[0].audio_effect);
  EXPECT_EQ(1, cras_observer_notify_nodes_called);

  // Remove pinned stream from d1, which means internal speaker device is
  // closed.
  stream_list_has_pinned_stream_ret[d1_.info.idx] = 0;
  EXPECT_EQ(0, stream_rm_cb(&rstream));
  EXPECT_EQ(1, cras_iodev_list_dev_is_enabled(&d1_));

  // Noise Cancellation is still blocked because internal speaker device is
  // still enabled. notify_nodes shouldn't be called.
  ASSERT_EQ(1, server_state_stub.num_input_nodes);
  EXPECT_EQ(default_audio_effect,
            server_state_stub.input_nodes[0].audio_effect);
  EXPECT_EQ(1, cras_observer_notify_nodes_called);

  // Disable internal speaker device d1.
  cras_iodev_list_disable_dev(&d1_, false);
  EXPECT_EQ(0, cras_iodev_list_dev_is_enabled(&d1_));

  // Noise Cancellation is unblocked because internal speaker device is
  // disabled (and closed).
  ASSERT_EQ(1, server_state_stub.num_input_nodes);
  EXPECT_EQ(node2.audio_effect, server_state_stub.input_nodes[0].audio_effect);
  EXPECT_EQ(2, cras_observer_notify_nodes_called);

  cras_iodev_list_deinit();
}

TEST_F(IoDevTestSuite, BlockNoiseCancellationByTwoNodesInOneDev) {
  struct cras_rstream rstream;
  struct cras_ionode node1_2;

  uint32_t default_audio_effect = 0x8000;

  cras_iodev_list_init();

  memset(&node1_2, 0, sizeof(node1_2));

  // Add output device with two nodes (node1 is active node).
  d1_.direction = CRAS_STREAM_OUTPUT;
  d1_.info.idx = 1;
  d1_.update_active_node = set_active_node_by_id;
  d1_.nodes = NULL;
  node1.idx = 1;
  node1.type = CRAS_NODE_TYPE_INTERNAL_SPEAKER;
  DL_APPEND(d1_.nodes, &node1);
  node1_2.idx = 2;
  node1_2.type = CRAS_NODE_TYPE_HEADPHONE;
  DL_APPEND(d1_.nodes, &node1_2);
  EXPECT_EQ(0, cras_iodev_list_add_output(&d1_));

  // Add input device for checking Noise Cancellation state.
  d2_.direction = CRAS_STREAM_INPUT;
  d2_.info.idx = 2;
  node2.idx = 2;
  node2.audio_effect = default_audio_effect | EFFECT_TYPE_NOISE_CANCELLATION;
  EXPECT_EQ(0, cras_iodev_list_add_input(&d2_));

  // Make sure shared state was updated.
  ASSERT_EQ(1, server_state_stub.num_input_nodes);
  EXPECT_EQ(node2.audio_effect, server_state_stub.input_nodes[0].audio_effect);
  EXPECT_EQ(0, cras_observer_notify_nodes_called);

  // Select internal speaker as the active node.
  cras_iodev_list_select_node(CRAS_STREAM_OUTPUT,
                              cras_make_node_id(d1_.info.idx, node1.idx));
  EXPECT_EQ(1, cras_iodev_list_dev_is_enabled(&d1_));

  // Noise Cancellation is blocked.
  ASSERT_EQ(1, server_state_stub.num_input_nodes);
  EXPECT_EQ(default_audio_effect,
            server_state_stub.input_nodes[0].audio_effect);
  EXPECT_EQ(1, cras_observer_notify_nodes_called);

  // Select headphone as the active node.
  cras_iodev_list_select_node(CRAS_STREAM_OUTPUT,
                              cras_make_node_id(d1_.info.idx, node1_2.idx));
  EXPECT_EQ(1, cras_iodev_list_dev_is_enabled(&d1_));

  // Noise Cancellation is unblocked.
  ASSERT_EQ(1, server_state_stub.num_input_nodes);
  EXPECT_EQ(node2.audio_effect, server_state_stub.input_nodes[0].audio_effect);
  EXPECT_EQ(2, cras_observer_notify_nodes_called);

  // Select internal speaker as the active node.
  cras_iodev_list_select_node(CRAS_STREAM_OUTPUT,
                              cras_make_node_id(d1_.info.idx, node1.idx));
  EXPECT_EQ(1, cras_iodev_list_dev_is_enabled(&d1_));

  // Noise Cancellation is blocked.
  ASSERT_EQ(1, server_state_stub.num_input_nodes);
  EXPECT_EQ(default_audio_effect,
            server_state_stub.input_nodes[0].audio_effect);
  EXPECT_EQ(3, cras_observer_notify_nodes_called);

  d1_.format = &fmt_;

  // Setup pinned streams.
  memset(&rstream, 0, sizeof(rstream));
  rstream.is_pinned = 1;
  rstream.pinned_dev_idx = d1_.info.idx;

  // Add pinned stream to d1, which means internal speaker device is opened.
  update_active_node_called = 0;
  EXPECT_EQ(0, stream_add_cb(&rstream));
  EXPECT_EQ(1, audio_thread_add_stream_called);
  EXPECT_EQ(&d1_, audio_thread_add_stream_dev);
  EXPECT_EQ(&rstream, audio_thread_add_stream_stream);
  EXPECT_EQ(1, update_active_node_called);
  EXPECT_EQ(1, cras_iodev_is_open(&d1_));

  // Select headphone as the active node.
  cras_iodev_list_select_node(CRAS_STREAM_OUTPUT,
                              cras_make_node_id(d1_.info.idx, node1_2.idx));
  EXPECT_EQ(1, cras_iodev_list_dev_is_enabled(&d1_));

  // Noise Cancellation is unblocked because headphone is the active node, and
  // the pinned stream is played by headphone.
  EXPECT_EQ(1, cras_iodev_list_dev_is_enabled(&d1_));
  ASSERT_EQ(1, server_state_stub.num_input_nodes);
  EXPECT_EQ(node2.audio_effect, server_state_stub.input_nodes[0].audio_effect);
  EXPECT_EQ(4, cras_observer_notify_nodes_called);

  // Remove pinned stream from d1.
  stream_list_has_pinned_stream_ret[d1_.info.idx] = 0;
  EXPECT_EQ(0, stream_rm_cb(&rstream));

  cras_iodev_list_deinit();
}

TEST(SoftvolCurveTest, InputNodeGainToDBFS) {
  for (long gain = 0; gain <= 100; ++gain) {
    long dBFS = convert_dBFS_from_input_node_gain(gain, false);
    EXPECT_EQ(dBFS, (gain - 50) * ((gain > 50) ? 2000 / 50 : 80));
    EXPECT_EQ(gain, convert_input_node_gain_from_dBFS(dBFS, 2000));
  }
}

TEST(SoftvolCurveTest, InternalMicGainToDBFS) {
  cras_system_get_max_internal_mic_gain_return = 1000;
  for (long gain = 0; gain <= 100; ++gain) {
    long dBFS = convert_dBFS_from_input_node_gain(gain, true);
    EXPECT_EQ(dBFS, (gain - 50) * ((gain > 50) ? 1000 / 50 : 80));
    EXPECT_EQ(gain, convert_input_node_gain_from_dBFS(dBFS, 1000));
  }
}

}  //  namespace

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}

extern "C" {

// Stubs

struct cras_server_state* cras_system_state_update_begin() {
  return server_state_update_begin_return;
}

void cras_system_state_update_complete() {}

int cras_system_get_mute() {
  return system_get_mute_return;
}

bool cras_system_get_noise_cancellation_enabled() {
  return false;
}

bool cras_system_get_bypass_block_noise_cancellation() {
  return false;
}

struct audio_thread* audio_thread_create() {
  return &thread;
}

int audio_thread_start(struct audio_thread* thread) {
  return 0;
}

void audio_thread_destroy(struct audio_thread* thread) {}

int audio_thread_set_active_dev(struct audio_thread* thread,
                                struct cras_iodev* dev) {
  audio_thread_set_active_dev_called++;
  audio_thread_set_active_dev_val = dev;
  return 0;
}

void audio_thread_remove_streams(struct audio_thread* thread,
                                 enum CRAS_STREAM_DIRECTION dir) {
  audio_thread_remove_streams_active_dev = audio_thread_set_active_dev_val;
}

int audio_thread_add_open_dev(struct audio_thread* thread,
                              struct cras_iodev* dev) {
  audio_thread_add_open_dev_dev = dev;
  audio_thread_add_open_dev_called++;
  return 0;
}

int audio_thread_rm_open_dev(struct audio_thread* thread,
                             enum CRAS_STREAM_DIRECTION dir,
                             unsigned int dev_idx) {
  audio_thread_rm_open_dev_called++;
  return 0;
}

int audio_thread_is_dev_open(struct audio_thread* thread,
                             struct cras_iodev* dev) {
  return audio_thread_is_dev_open_ret;
}

int audio_thread_add_stream(struct audio_thread* thread,
                            struct cras_rstream* stream,
                            struct cras_iodev** devs,
                            unsigned int num_devs) {
  audio_thread_add_stream_called++;
  audio_thread_add_stream_stream = stream;
  audio_thread_add_stream_dev = (num_devs ? devs[0] : NULL);
  return 0;
}

int audio_thread_disconnect_stream(struct audio_thread* thread,
                                   struct cras_rstream* stream,
                                   struct cras_iodev* iodev) {
  audio_thread_disconnect_stream_called++;
  audio_thread_disconnect_stream_stream = stream;
  audio_thread_disconnect_stream_dev = iodev;
  return 0;
}

int audio_thread_drain_stream(struct audio_thread* thread,
                              struct cras_rstream* stream) {
  audio_thread_drain_stream_called++;
  return audio_thread_drain_stream_return;
}

struct cras_iodev* empty_iodev_create(enum CRAS_STREAM_DIRECTION direction,
                                      enum CRAS_NODE_TYPE node_type) {
  struct cras_iodev* dev;
  if (node_type == CRAS_NODE_TYPE_HOTWORD) {
    dev = &mock_hotword_iodev;
  } else {
    dev = &mock_empty_iodev[direction];
  }
  dev->direction = direction;
  if (dev->active_node == NULL) {
    struct cras_ionode* node = (struct cras_ionode*)calloc(1, sizeof(*node));
    node->type = node_type;
    dev->active_node = node;
  }
  return dev;
}

void empty_iodev_destroy(struct cras_iodev* iodev) {
  if (iodev->active_node) {
    free(iodev->active_node);
    iodev->active_node = NULL;
  }
}

struct cras_iodev* test_iodev_create(enum CRAS_STREAM_DIRECTION direction,
                                     enum TEST_IODEV_TYPE type) {
  return NULL;
}

void test_iodev_command(struct cras_iodev* iodev,
                        enum CRAS_TEST_IODEV_CMD command,
                        unsigned int data_len,
                        const uint8_t* data) {}

struct cras_iodev* loopback_iodev_create(enum CRAS_LOOPBACK_TYPE type) {
  return &loopback_input;
}

void loopback_iodev_destroy(struct cras_iodev* iodev) {}

int cras_iodev_open(struct cras_iodev* iodev,
                    unsigned int cb_level,
                    const struct cras_audio_format* fmt) {
  if (cras_iodev_open_ret[cras_iodev_open_called] == 0)
    iodev->state = CRAS_IODEV_STATE_OPEN;
  cras_iodev_open_fmt = *fmt;
  iodev->format = &cras_iodev_open_fmt;
  return cras_iodev_open_ret[cras_iodev_open_called++];
}

int cras_iodev_close(struct cras_iodev* iodev) {
  iodev->state = CRAS_IODEV_STATE_CLOSE;
  cras_iodev_close_called++;
  cras_iodev_close_dev = iodev;
  iodev->format = NULL;
  return 0;
}

int cras_iodev_set_format(struct cras_iodev* iodev,
                          const struct cras_audio_format* fmt) {
  return 0;
}

int cras_iodev_set_mute(struct cras_iodev* iodev) {
  set_mute_called++;
  set_mute_dev_vector.push_back(iodev);
  return 0;
}

void cras_iodev_set_node_plugged(struct cras_ionode* node, int plugged) {
  set_node_plugged_called++;
}

bool cras_iodev_support_noise_cancellation(const struct cras_iodev* iodev,
                                           unsigned node_idx) {
  return true;
}

int cras_iodev_start_volume_ramp(struct cras_iodev* odev,
                                 unsigned int old_volume,
                                 unsigned int new_volume) {
  cras_iodev_start_volume_ramp_called++;
  return 0;
}
bool cras_iodev_is_aec_use_case(const struct cras_ionode* node) {
  return 1;
}
bool stream_list_has_pinned_stream(struct stream_list* list,
                                   unsigned int dev_idx) {
  return stream_list_has_pinned_stream_ret[dev_idx];
}

struct stream_list* stream_list_create(stream_callback* add_cb,
                                       stream_callback* rm_cb,
                                       stream_create_func* create_cb,
                                       stream_destroy_func* destroy_cb,
                                       struct cras_tm* timer_manager) {
  stream_add_cb = add_cb;
  stream_rm_cb = rm_cb;
  return reinterpret_cast<struct stream_list*>(0xf00);
}

void stream_list_destroy(struct stream_list* list) {}

struct cras_rstream* stream_list_get(struct stream_list* list) {
  return stream_list_get_ret;
}
void server_stream_create(struct stream_list* stream_list,
                          unsigned int dev_idx,
                          struct cras_audio_format* format) {
  server_stream_create_called++;
}
void server_stream_destroy(struct stream_list* stream_list,
                           unsigned int dev_idx) {
  server_stream_destroy_called++;
}

int cras_rstream_create(struct cras_rstream_config* config,
                        struct cras_rstream** stream_out) {
  return 0;
}

void cras_rstream_destroy(struct cras_rstream* rstream) {}

struct cras_tm* cras_system_state_get_tm() {
  return NULL;
}

const char* cras_system_state_get_active_node_types() {
  return NULL;
}

struct cras_timer* cras_tm_create_timer(struct cras_tm* tm,
                                        unsigned int ms,
                                        void (*cb)(struct cras_timer* t,
                                                   void* data),
                                        void* cb_data) {
  cras_tm_timer_cb = cb;
  cras_tm_timer_cb_data = cb_data;
  cras_tm_create_timer_called++;
  return reinterpret_cast<struct cras_timer*>(0x404);
}

void cras_tm_cancel_timer(struct cras_tm* tm, struct cras_timer* t) {
  cras_tm_cancel_timer_called++;
}

void cras_fmt_conv_destroy(struct cras_fmt_conv* conv) {}

struct cras_fmt_conv* cras_channel_remix_conv_create(unsigned int num_channels,
                                                     const float* coefficient) {
  return NULL;
}

void cras_channel_remix_convert(struct cras_fmt_conv* conv,
                                uint8_t* in_buf,
                                size_t frames) {}

struct cras_observer_client* cras_observer_add(
    const struct cras_observer_ops* ops,
    void* context) {
  observer_ops = (struct cras_observer_ops*)calloc(1, sizeof(*ops));
  memcpy(observer_ops, ops, sizeof(*ops));
  cras_observer_add_called++;
  return reinterpret_cast<struct cras_observer_client*>(0x55);
}

void cras_observer_remove(struct cras_observer_client* client) {
  if (observer_ops)
    free(observer_ops);
  cras_observer_remove_called++;
}

void cras_observer_notify_nodes(void) {
  cras_observer_notify_nodes_called++;
}

void cras_observer_notify_active_node(enum CRAS_STREAM_DIRECTION direction,
                                      cras_node_id_t node_id) {
  cras_observer_notify_active_node_called++;
}

void cras_observer_notify_output_node_volume(cras_node_id_t node_id,
                                             int32_t volume) {
  cras_observer_notify_output_node_volume_called++;
}

void cras_observer_notify_node_left_right_swapped(cras_node_id_t node_id,
                                                  int swapped) {
  cras_observer_notify_node_left_right_swapped_called++;
}

void cras_observer_notify_input_node_gain(cras_node_id_t node_id,
                                          int32_t gain) {
  cras_observer_notify_input_node_gain_called++;
}

int audio_thread_dev_start_ramp(struct audio_thread* thread,
                                unsigned int dev_idx,
                                enum CRAS_IODEV_RAMP_REQUEST request) {
  audio_thread_dev_start_ramp_called++;
  audio_thread_dev_start_ramp_dev_vector.push_back(dev_idx);
  audio_thread_dev_start_ramp_req = request;
  return 0;
}

struct cras_apm* cras_stream_apm_add(struct cras_stream_apm* stream,
                                     struct cras_iodev* idev,
                                     const struct cras_audio_format* fmt) {
  cras_stream_apm_add_called++;
  return NULL;
}
void cras_stream_apm_remove(struct cras_stream_apm* stream,
                            const struct cras_iodev* idev) {
  cras_stream_apm_remove_called++;
}
int cras_stream_apm_init(const char* device_config_dir) {
  return 0;
}
int cras_stream_apm_set_aec_ref(struct cras_stream_apm* stream,
                                struct cras_iodev* echo_ref) {
  cras_stream_apm_set_aec_ref_called++;
  return 0;
}

//  From librt.
int clock_gettime(clockid_t clk_id, struct timespec* tp) {
  tp->tv_sec = clock_gettime_retspec.tv_sec;
  tp->tv_nsec = clock_gettime_retspec.tv_nsec;
  return 0;
}

bool cras_system_get_hotword_pause_at_suspend() {
  return !!server_state_hotword_pause_at_suspend;
}

bool cras_iodev_is_node_internal_mic(const struct cras_ionode* node) {
  if (node->type == CRAS_NODE_TYPE_MIC) {
    return (node->position == NODE_POSITION_INTERNAL) ||
           (node->position == NODE_POSITION_FRONT) ||
           (node->position == NODE_POSITION_REAR);
  }
  return false;
}

int cras_system_get_max_internal_mic_gain() {
  return cras_system_get_max_internal_mic_gain_return;
}

void cras_hats_trigger_general_survey(enum CRAS_STREAM_TYPE stream_type,
                                      enum CRAS_CLIENT_TYPE client_type,
                                      const char* node_type_pair) {}

bool cras_floop_pair_match_output_stream(const struct cras_floop_pair* pair,
                                         const struct cras_rstream* stream) {
  return false;
}
int cras_server_metrics_set_aec_ref_device_type(struct cras_iodev* iodev) {
  return 0;
}
int cras_server_metrics_stream_add_failure(enum CRAS_STREAM_ADD_ERROR code) {
  return 0;
}

}  // extern "C"
