// Copyright 2012 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <algorithm>
#include <gtest/gtest.h>
#include <map>
#include <stdio.h>

#include "cras/src/tests/test_util.h"
#include "cras_iodev_info.h"

extern "C" {
#include "cras/src/common/cras_observer_ops.h"
#include "cras/src/server/audio_thread.h"
#include "cras/src/server/cras_iodev.h"
#include "cras/src/server/cras_iodev_list.c"
#include "cras/src/server/cras_main_thread_log.h"
#include "cras/src/server/cras_ramp.h"
#include "cras/src/server/cras_rstream.h"
#include "cras/src/server/cras_server_metrics.h"
#include "cras/src/server/cras_system_state.h"
#include "cras/src/server/cras_tm.h"
#include "cras/src/server/stream_list.h"
#include "cras/src/tests/scoped_features_override.h"
#include "third_party/utlist/utlist.h"
}

namespace {

struct cras_server_state server_state_stub;
struct cras_server_state* server_state_update_begin_return;
int system_get_mute_return;

// Data for stubs.
static struct cras_observer_ops* observer_ops;
static unsigned int set_node_plugged_called;
static cras_iodev* audio_thread_remove_streams_active_dev;
static cras_iodev* audio_thread_set_active_dev_val;
static int audio_thread_set_active_dev_called;
static cras_iodev* audio_thread_add_open_dev_dev;
// Note that the following two variables are exclusive
static int audio_thread_add_open_dev_called;
static int audio_thread_add_open_dev_fallback_called;
static int audio_thread_rm_open_dev_called;
static int audio_thread_rm_open_dev_fallback_called;
static int audio_thread_is_dev_open_ret;
static struct audio_thread thread;
static struct cras_iodev loopback_input;
static int cras_iodev_close_called;
static int cras_iodev_close_fallback_called;
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
static int audio_thread_add_stream_fallback_called;
static unsigned update_active_node_called;
static struct cras_iodev* update_active_node_iodev_val[16];
static unsigned update_active_node_node_idx_val[16];
static unsigned update_active_node_dev_enabled_val[16];
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
static int cras_observer_notify_input_node_gain_value;
/* Do no reset cras_iodev_open_called unless it is certain that it is fine to
 * overwrite the format of previously opened iodev */
static int cras_iodev_open_called;
static int cras_iodev_open_fallback_called;
static int cras_iodev_open_ret[8];
static struct cras_audio_format cras_iodev_open_fmt[8];
static struct cras_audio_format cras_iodev_open_fallback_fmt;
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
static struct cras_floop_pair* cras_floop_pair_create_return;

int dev_idx_in_vector(std::vector<unsigned int> v, unsigned int idx) {
  return std::find(v.begin(), v.end(), idx) != v.end();
}

int device_in_vector(std::vector<struct cras_iodev*> v,
                     struct cras_iodev* dev) {
  return std::find(v.begin(), v.end(), dev) != v.end();
}

template <class TestBase>
class IodevTests : public TestBase {
 protected:
  void ResetStubData() {
    audio_thread_add_open_dev_fallback_called = 0;
    audio_thread_rm_open_dev_fallback_called = 0;
    audio_thread_add_stream_fallback_called = 0;
    cras_iodev_open_fallback_called = 0;
    cras_iodev_close_fallback_called = 0;

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

    // Reset stub data.
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
    cras_observer_notify_input_node_gain_value = 0;
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
    memset(update_active_node_iodev_val, 0,
           sizeof(update_active_node_iodev_val));
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
    cras_floop_pair_create_return = NULL;
  }
  void SetUp() override {
    cras_iodev_list_reset();
    ResetStubData();
  }

  void TearDown() override { cras_iodev_list_reset(); }

  static void update_active_node(struct cras_iodev* iodev,
                                 unsigned node_idx,
                                 unsigned dev_enabled) {
    int i = update_active_node_called++;
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

  struct cras_ionode node1, node2, node3;
};

class IoDevTestSuite : public IodevTests<testing::Test> {
 protected:
};

// Check that Init registers observer client. */
TEST_F(IoDevTestSuite, InitSetup) {
  {
    CLEAR_AND_EVENTUALLY(EXPECT_EQ, cras_observer_add_called, 1);

    cras_iodev_list_init();
  }
  {
    CLEAR_AND_EVENTUALLY(EXPECT_EQ, cras_observer_remove_called, 1);

    cras_iodev_list_deinit();
  }
}

/* Check that the suspend alert from cras_system will trigger suspend
 * and resume call of all iodevs. */
TEST_F(IoDevTestSuite, SetSuspendResume) {
  struct cras_rstream rstream, rstream2, rstream3;
  int rc;

  memset(&rstream, 0, sizeof(rstream));
  memset(&rstream2, 0, sizeof(rstream2));
  memset(&rstream3, 0, sizeof(rstream3));

  cras_iodev_list_init();

  d1_.info.idx = 1;
  d1_.direction = CRAS_STREAM_OUTPUT;
  rc = cras_iodev_list_add_output(&d1_);
  ASSERT_EQ(rc, 0);

  d2_.info.idx = 2;
  d2_.direction = CRAS_STREAM_OUTPUT;
  rc = cras_iodev_list_add_output(&d2_);
  ASSERT_EQ(rc, 0);

  d1_.format = &fmt_;

  {
    CLEAR_AND_EVENTUALLY(EXPECT_EQ, cras_observer_notify_active_node_called, 1);

    cras_iodev_list_select_node(CRAS_STREAM_OUTPUT,
                                cras_make_node_id(d1_.info.idx, 0));
  }

  {
    CLEAR_AND_EVENTUALLY(EXPECT_EQ, audio_thread_add_open_dev_called, 1);
    CLEAR_AND_EVENTUALLY(EXPECT_EQ, audio_thread_add_stream_called, 1);

    DL_APPEND(stream_list_get_ret, &rstream);
    stream_add_cb(&rstream);
  }

  {
    CLEAR_AND_EVENTUALLY(EXPECT_EQ, audio_thread_add_stream_called, 1);

    DL_APPEND(stream_list_get_ret, &rstream2);
    stream_add_cb(&rstream2);
  }

  {
    CLEAR_AND_EVENTUALLY(EXPECT_EQ, audio_thread_rm_open_dev_called, 1);
    CLEAR_AND_EVENTUALLY(EXPECT_EQ, cras_observer_notify_active_node_called, 0);

    observer_ops->suspend_changed(NULL, 1);
  }

  {  // Test disable/enable dev won't cause add_stream to audio_thread.
    CLEAR_AND_EVENTUALLY(EXPECT_EQ, audio_thread_add_stream_called, 0);
    CLEAR_AND_EVENTUALLY(EXPECT_EQ, cras_observer_notify_active_node_called, 2);

    cras_iodev_list_select_node(CRAS_STREAM_OUTPUT,
                                cras_make_node_id(d2_.info.idx, 0));
    cras_iodev_list_select_node(CRAS_STREAM_OUTPUT,
                                cras_make_node_id(d1_.info.idx, 0));
  }

  {
    CLEAR_AND_EVENTUALLY(EXPECT_EQ, audio_thread_drain_stream_called, 1);

    audio_thread_drain_stream_return = 0;

    EVENTUALLY(EXPECT_EQ, rc, 0);

    DL_DELETE(stream_list_get_ret, &rstream2);
    rc = stream_rm_cb(&rstream2);
  }

  {  // Test stream_add_cb won't cause add_stream to audio_thread.
    CLEAR_AND_EVENTUALLY(EXPECT_EQ, audio_thread_add_stream_called, 0);

    DL_APPEND(stream_list_get_ret, &rstream3);
    stream_add_cb(&rstream3);
  }

  {  // On resume will reopen d1_, add rstream, and add rstream3.
    CLEAR_AND_EVENTUALLY(EXPECT_EQ, audio_thread_add_open_dev_called, 1);
    CLEAR_AND_EVENTUALLY(EXPECT_EQ, audio_thread_add_stream_called, 2);
    CLEAR_AND_EVENTUALLY(EXPECT_EQ, audio_thread_add_stream_stream, &rstream3);
    CLEAR_AND_EVENTUALLY(EXPECT_EQ, cras_observer_notify_active_node_called, 0);

    observer_ops->suspend_changed(NULL, 0);
  }

  cras_iodev_list_deinit();
}

/* Check that the suspend/resume call of active iodev will be triggered and
 * fallback device will be transciently enabled while adding a new stream whose
 * channel count is higher than the active iodev. */
TEST_F(IoDevTestSuite, ReopenDevForHigherChannels) {
  struct cras_rstream rstream, rstream2;
  int rc;

  memset(&rstream, 0, sizeof(rstream));
  memset(&rstream2, 0, sizeof(rstream2));
  rstream.format = fmt_;
  rstream2.format = fmt_;
  rstream2.format.num_channels = 6;

  cras_iodev_list_init();

  d1_.direction = CRAS_STREAM_OUTPUT;
  rc = cras_iodev_list_add_output(&d1_);
  ASSERT_EQ(rc, 0);

  d1_.format = &fmt_;
  d1_.info.max_supported_channels = 2;

  cras_iodev_list_add_active_node(CRAS_STREAM_OUTPUT,
                                  cras_make_node_id(d1_.info.idx, 1));

  /* Note that we shouldn't clear cras_iodev_open_called, otherwise the
   * formats of the iodevs will be overwritten, which can change the code
   * path of interest in this test */

  {
    CLEAR_AND_EVENTUALLY(EXPECT_EQ, audio_thread_add_stream_called, 1);
    CLEAR_AND_EVENTUALLY(EXPECT_EQ, audio_thread_add_open_dev_called, 1);

    const int prev_cras_iodev_open_called = cras_iodev_open_called;

    EVENTUALLY(EXPECT_EQ, cras_iodev_open_called - prev_cras_iodev_open_called,
               1);

    EVENTUALLY(EXPECT_EQ,
               cras_iodev_open_fmt[cras_iodev_open_called - 1].num_channels, 2);

    DL_APPEND(stream_list_get_ret, &rstream);
    stream_add_cb(&rstream);
  }

  { /* The channel count(=6) of rstream2 exceeds d1's
     * max_supported_channels(=2), rstream2 will be added directly to d1, which
     * will not re-open d1. */
    CLEAR_AND_EVENTUALLY(EXPECT_EQ, audio_thread_add_stream_called, 1);
    CLEAR_AND_EVENTUALLY(EXPECT_EQ, audio_thread_add_open_dev_called, 0);
    EVENTUALLY(EXPECT_NE, d1_.format->num_channels,
               rstream2.format.num_channels);

    const int prev_cras_iodev_open_called = cras_iodev_open_called;
    EVENTUALLY(EXPECT_EQ, cras_iodev_open_called - prev_cras_iodev_open_called,
               0);

    // stream_list should be descending ordered by channel count.
    DL_PREPEND(stream_list_get_ret, &rstream2);
    stream_add_cb(&rstream2);
  }

  d1_.info.max_supported_channels = 6;

  DL_DELETE(stream_list_get_ret, &rstream2);
  stream_rm_cb(&rstream2);

  {  // Added both rstreams to fallback device, then re-opened d1.
    CLEAR_AND_EVENTUALLY(EXPECT_EQ, cras_iodev_open_fallback_called, 1);
    CLEAR_AND_EVENTUALLY(EXPECT_EQ, audio_thread_add_stream_fallback_called, 2);
    CLEAR_AND_EVENTUALLY(EXPECT_EQ, audio_thread_add_open_dev_fallback_called,
                         1);
    CLEAR_AND_EVENTUALLY(EXPECT_EQ, audio_thread_add_stream_called, 2);
    CLEAR_AND_EVENTUALLY(EXPECT_EQ, audio_thread_add_open_dev_called, 1);

    const int prev_cras_iodev_open_called = cras_iodev_open_called;
    EVENTUALLY(EXPECT_EQ, cras_iodev_open_called - prev_cras_iodev_open_called,
               1);
    EVENTUALLY(EXPECT_EQ,
               cras_iodev_open_fmt[cras_iodev_open_called - 1].num_channels, 6);
    EVENTUALLY(EXPECT_EQ, d1_.format->num_channels,
               rstream2.format.num_channels);

    DL_PREPEND(stream_list_get_ret, &rstream2);
    stream_add_cb(&rstream2);
  }

  cras_iodev_list_deinit();
}

/* Simulate a corner case where reopen happens after a sequence of open failures
 * and with a fallback in the enabled_devs */
TEST_F(IoDevTestSuite, DevOpenFailsFollowedByReopenDev) {
  struct cras_rstream rstream, rstream2;
  int rc;

  memset(&rstream, 0, sizeof(rstream));
  memset(&rstream2, 0, sizeof(rstream2));
  rstream.format = fmt_;
  rstream2.format = fmt_;
  rstream2.format.num_channels = 6;

  cras_iodev_list_init();

  d1_.direction = CRAS_STREAM_OUTPUT;
  rc = cras_iodev_list_add_output(&d1_);
  ASSERT_EQ(rc, 0);

  d1_.format = &fmt_;
  d1_.info.max_supported_channels = 2;

  {  // d1_ is closed since there is no stream
    EVENTUALLY(EXPECT_EQ, d1_.state, CRAS_IODEV_STATE_CLOSE);

    cras_iodev_list_select_node(CRAS_STREAM_OUTPUT,
                                cras_make_node_id(d1_.info.idx, 0));
  }

  {  // Trigger stream_add_cb while d1_ is closed, and fail init_device
    CLEAR_AND_EVENTUALLY(EXPECT_EQ, cras_iodev_open_fallback_called, 1);
    CLEAR_AND_EVENTUALLY(EXPECT_EQ, cras_iodev_close_fallback_called, 0);
    EVENTUALLY(EXPECT_EQ, d1_.state, CRAS_IODEV_STATE_CLOSE);

    cras_iodev_open_ret[cras_iodev_open_called] = -5;

    DL_APPEND(stream_list_get_ret, &rstream);
    stream_add_cb(&rstream);
  }

  {  // Open d1_
    CLEAR_AND_EVENTUALLY(EXPECT_EQ, audio_thread_add_stream_called, 1);
    EVENTUALLY(EXPECT_EQ, d1_.state, CRAS_IODEV_STATE_OPEN);

    cras_iodev_open_ret[cras_iodev_open_called] = 0;

    stream_add_cb(&rstream);
  }

  { /* Simulate a corner case where the reopen happens with a normal dev
       followed by a fallback dev */
    CLEAR_AND_EVENTUALLY(EXPECT_EQ, audio_thread_add_stream_called, 2);
    CLEAR_AND_EVENTUALLY(EXPECT_EQ, cras_iodev_close_fallback_called, 1);
    EVENTUALLY(EXPECT_EQ, d1_.format->num_channels,
               rstream2.format.num_channels);

    cras_iodev_open_ret[cras_iodev_open_called] = 0;

    d1_.format->num_channels = 2;
    d1_.info.max_supported_channels = 6;

    DL_PREPEND(stream_list_get_ret, &rstream2);
    stream_add_cb(&rstream2);
  }

  cras_iodev_list_deinit();
}

/* Check that after resume, all output devices enter ramp mute state if there is
 * any output stream. */
TEST_F(IoDevTestSuite, RampMuteAfterResume) {
  struct cras_rstream rstream, rstream2;
  int rc;

  memset(&rstream, 0, sizeof(rstream));

  cras_iodev_list_init();

  d1_.direction = CRAS_STREAM_OUTPUT;
  d1_.initial_ramp_request = CRAS_IODEV_RAMP_REQUEST_UP_START_PLAYBACK;
  rc = cras_iodev_list_add_output(&d1_);
  ASSERT_EQ(rc, 0);

  d2_.direction = CRAS_STREAM_INPUT;
  d2_.initial_ramp_request = CRAS_IODEV_RAMP_REQUEST_UP_START_PLAYBACK;
  rc = cras_iodev_list_add_input(&d2_);
  ASSERT_EQ(rc, 0);

  d1_.format = &fmt_;
  d2_.format = &fmt_;

  cras_iodev_list_add_active_node(CRAS_STREAM_OUTPUT,
                                  cras_make_node_id(d1_.info.idx, 1));

  {
    CLEAR_AND_EVENTUALLY(EXPECT_EQ, audio_thread_add_stream_called, 1);
    CLEAR_AND_EVENTUALLY(EXPECT_EQ, audio_thread_add_open_dev_called, 1);

    DL_APPEND(stream_list_get_ret, &rstream);
    stream_add_cb(&rstream);
  }

  rstream2.direction = CRAS_STREAM_INPUT;
  DL_APPEND(stream_list_get_ret, &rstream2);
  stream_add_cb(&rstream2);

  // Suspend and resume
  observer_ops->suspend_changed(NULL, 1);
  observer_ops->suspend_changed(NULL, 0);

  // Test only output device that has stream will be muted after resume
  EXPECT_EQ(d1_.initial_ramp_request, CRAS_IODEV_RAMP_REQUEST_RESUME_MUTE);
  EXPECT_EQ(d2_.initial_ramp_request,
            CRAS_IODEV_RAMP_REQUEST_UP_START_PLAYBACK);

  // Reset d1 ramp_mute and remove output stream to test again
  d1_.initial_ramp_request = CRAS_IODEV_RAMP_REQUEST_UP_START_PLAYBACK;

  DL_DELETE(stream_list_get_ret, &rstream);
  stream_rm_cb(&rstream);

  // Suspend and resume
  observer_ops->suspend_changed(NULL, 1);
  observer_ops->suspend_changed(NULL, 0);

  EXPECT_EQ(d1_.initial_ramp_request,
            CRAS_IODEV_RAMP_REQUEST_UP_START_PLAYBACK);
  EXPECT_EQ(d2_.initial_ramp_request,
            CRAS_IODEV_RAMP_REQUEST_UP_START_PLAYBACK);

  cras_iodev_list_deinit();
}

TEST_F(IoDevTestSuite, InitDevFailShouldEnableFallback) {
  int rc;
  struct cras_rstream rstream;

  memset(&rstream, 0, sizeof(rstream));
  cras_iodev_list_init();

  d1_.direction = CRAS_STREAM_OUTPUT;
  rc = cras_iodev_list_add_output(&d1_);
  ASSERT_EQ(rc, 0);

  d1_.format = &fmt_;

  cras_iodev_list_select_node(CRAS_STREAM_OUTPUT,
                              cras_make_node_id(d1_.info.idx, 0));

  {  // Open d1_ fails, and make sure fallback is opened.
    CLEAR_AND_EVENTUALLY(EXPECT_EQ, cras_iodev_open_called, 1);
    CLEAR_AND_EVENTUALLY(EXPECT_EQ, cras_iodev_open_fallback_called, 1);
    CLEAR_AND_EVENTUALLY(EXPECT_EQ, audio_thread_add_stream_fallback_called, 1);

    cras_iodev_open_ret[cras_iodev_open_called] = -5;

    DL_APPEND(stream_list_get_ret, &rstream);
    stream_add_cb(&rstream);
  }

  cras_iodev_list_deinit();
}

TEST_F(IoDevTestSuite, InitDevWithEchoRef) {
  int rc;
  struct cras_rstream rstream;

  memset(&rstream, 0, sizeof(rstream));
  cras_iodev_list_init();

  d1_.direction = CRAS_STREAM_OUTPUT;
  d1_.echo_reference_dev = &d2_;
  rc = cras_iodev_list_add_output(&d1_);
  ASSERT_EQ(rc, 0);

  d2_.direction = CRAS_STREAM_INPUT;
  snprintf(d2_.active_node->name, CRAS_NODE_NAME_BUFFER_SIZE, "echo ref");
  rc = cras_iodev_list_add_input(&d2_);
  ASSERT_EQ(rc, 0);

  d1_.format = &fmt_;
  d2_.format = &fmt_;

  {  // No close call should happen, because no stream exists.
    CLEAR_AND_EVENTUALLY(EXPECT_EQ, cras_iodev_close_called, 0);

    cras_iodev_list_select_node(CRAS_STREAM_OUTPUT,
                                cras_make_node_id(d1_.info.idx, 0));
  }

  {
    CLEAR_AND_EVENTUALLY(EXPECT_EQ, cras_iodev_open_called, 1);
    CLEAR_AND_EVENTUALLY(EXPECT_EQ, audio_thread_add_stream_called, 1);
    CLEAR_AND_EVENTUALLY(EXPECT_EQ, server_stream_create_called, 1);

    DL_APPEND(stream_list_get_ret, &rstream);
    stream_add_cb(&rstream);
  }

  DL_DELETE(stream_list_get_ret, &rstream);
  stream_rm_cb(&rstream);

  {
    CLEAR_AND_EVENTUALLY(EXPECT_EQ, cras_iodev_close_called, 1);
    CLEAR_AND_EVENTUALLY(EXPECT_EQ, server_stream_destroy_called, 1);

    clock_gettime_retspec.tv_sec = 11;
    clock_gettime_retspec.tv_nsec = 0;

    cras_tm_timer_cb(NULL, NULL);
  }

  cras_iodev_list_deinit();
}

TEST_F(IoDevTestSuite, SelectNodeOpenFailShouldScheduleRetry) {
  struct cras_rstream rstream;
  int rc;

  memset(&rstream, 0, sizeof(rstream));
  cras_iodev_list_init();

  d1_.direction = CRAS_STREAM_OUTPUT;
  rc = cras_iodev_list_add_output(&d1_);
  ASSERT_EQ(rc, 0);

  d2_.direction = CRAS_STREAM_OUTPUT;
  rc = cras_iodev_list_add_output(&d2_);
  ASSERT_EQ(rc, 0);

  d1_.format = &fmt_;
  d2_.format = &fmt_;

  cras_iodev_list_select_node(CRAS_STREAM_OUTPUT,
                              cras_make_node_id(d1_.info.idx, 1));

  DL_APPEND(stream_list_get_ret, &rstream);
  stream_add_cb(&rstream);

  {  // Select node triggers fallback open, d1 close, d2 open, fallback close.
    CLEAR_AND_EVENTUALLY(EXPECT_EQ, cras_iodev_open_fallback_called, 1);
    CLEAR_AND_EVENTUALLY(EXPECT_EQ, cras_iodev_close_called, 1);
    CLEAR_AND_EVENTUALLY(EXPECT_EQ, cras_iodev_open_called, 1);
    CLEAR_AND_EVENTUALLY(EXPECT_EQ, cras_iodev_close_fallback_called, 1);
    CLEAR_AND_EVENTUALLY(EXPECT_EQ, cras_tm_create_timer_called, 0);
    CLEAR_AND_EVENTUALLY(EXPECT_EQ, cras_tm_cancel_timer_called, 0);

    cras_iodev_list_select_node(CRAS_STREAM_OUTPUT,
                                cras_make_node_id(d2_.info.idx, 1));
  }

  { /* Select node d1_ but failing to open it will result in the sequence:
       fallback open, d2_ close, d1_ open (but fails) */
    CLEAR_AND_EVENTUALLY(EXPECT_EQ, cras_iodev_open_fallback_called, 1);

    // The fallback shouldn't close, only d2_ should be closed.
    CLEAR_AND_EVENTUALLY(EXPECT_EQ, cras_iodev_close_called, 1);
    CLEAR_AND_EVENTUALLY(EXPECT_EQ, cras_iodev_close_dev, &d2_);
    CLEAR_AND_EVENTUALLY(EXPECT_EQ, cras_iodev_close_fallback_called, 0);

    CLEAR_AND_EVENTUALLY(EXPECT_EQ, cras_iodev_open_called, 1);

    // Assert a timer will be scheduled to retry open.
    CLEAR_AND_EVENTUALLY(EXPECT_NE, cras_tm_timer_cb, nullptr);
    CLEAR_AND_EVENTUALLY(EXPECT_EQ, cras_tm_create_timer_called, 1);
    CLEAR_AND_EVENTUALLY(EXPECT_EQ, cras_tm_cancel_timer_called, 0);

    cras_iodev_open_ret[cras_iodev_open_called] = -5;

    cras_iodev_list_select_node(CRAS_STREAM_OUTPUT,
                                cras_make_node_id(d1_.info.idx, 1));
  }

  {  // Retry open success will close fallback dev.
    CLEAR_AND_EVENTUALLY(EXPECT_EQ, cras_iodev_open_called, 1);
    CLEAR_AND_EVENTUALLY(EXPECT_EQ, audio_thread_add_stream_called, 1);
    CLEAR_AND_EVENTUALLY(EXPECT_EQ, cras_iodev_close_fallback_called, 1);
    CLEAR_AND_EVENTUALLY(EXPECT_EQ, cras_tm_cancel_timer_called, 0);

    cras_iodev_open_ret[cras_iodev_open_called] = 0;

    cras_tm_timer_cb(NULL, cras_tm_timer_cb_data);
  }

  {  // Select d2_ and simulate an open failure.
    CLEAR_AND_EVENTUALLY(EXPECT_EQ, cras_iodev_open_called, 1);
    CLEAR_AND_EVENTUALLY(EXPECT_EQ, cras_iodev_open_fallback_called, 1);
    CLEAR_AND_EVENTUALLY(EXPECT_EQ, cras_iodev_close_called, 1);
    CLEAR_AND_EVENTUALLY(EXPECT_EQ, cras_iodev_close_fallback_called, 0);
    CLEAR_AND_EVENTUALLY(EXPECT_EQ, cras_iodev_close_dev, &d1_);
    CLEAR_AND_EVENTUALLY(EXPECT_EQ, cras_tm_create_timer_called, 1);
    CLEAR_AND_EVENTUALLY(EXPECT_NE, cras_tm_timer_cb, nullptr);

    cras_iodev_open_ret[cras_iodev_open_called] = -5;

    cras_iodev_list_select_node(CRAS_STREAM_OUTPUT,
                                cras_make_node_id(d2_.info.idx, 1));
  }

  {  // Selecting another iodev should cancel the timer.
    CLEAR_AND_EVENTUALLY(EXPECT_EQ, cras_tm_cancel_timer_called, 1);
    CLEAR_AND_EVENTUALLY(EXPECT_EQ, cras_iodev_close_fallback_called, 1);

    cras_iodev_list_select_node(CRAS_STREAM_OUTPUT,
                                cras_make_node_id(d2_.info.idx, 1));
  }

  cras_iodev_list_deinit();
}

TEST_F(IoDevTestSuite, InitDevFailShouldScheduleRetry) {
  int rc;
  struct cras_rstream rstream;

  memset(&rstream, 0, sizeof(rstream));
  rstream.format = fmt_;
  cras_iodev_list_init();

  d1_.direction = CRAS_STREAM_OUTPUT;
  rc = cras_iodev_list_add_output(&d1_);
  ASSERT_EQ(rc, 0);

  d1_.format = &fmt_;

  cras_iodev_list_select_node(CRAS_STREAM_OUTPUT,
                              cras_make_node_id(d1_.info.idx, 0));

  {  // Open d1_ fails, and make sure fallback is opened.
    CLEAR_AND_EVENTUALLY(EXPECT_EQ, cras_iodev_open_called, 1);
    CLEAR_AND_EVENTUALLY(EXPECT_EQ, cras_iodev_open_fallback_called, 1);
    CLEAR_AND_EVENTUALLY(EXPECT_EQ, audio_thread_add_stream_fallback_called, 1);
    CLEAR_AND_EVENTUALLY(EXPECT_EQ, audio_thread_add_stream_dev,
                         fallback_devs[CRAS_STREAM_OUTPUT]);
    CLEAR_AND_EVENTUALLY(EXPECT_EQ, update_active_node_called, 0);
    CLEAR_AND_EVENTUALLY(EXPECT_EQ, cras_tm_create_timer_called, 1);
    CLEAR_AND_EVENTUALLY(EXPECT_NE, cras_tm_timer_cb, nullptr);

    cras_iodev_open_ret[cras_iodev_open_called] = -5;

    DL_APPEND(stream_list_get_ret, &rstream);
    stream_add_cb(&rstream);
  }

  {  // If retry still fails, won't schedule more retries.
    CLEAR_AND_EVENTUALLY(EXPECT_EQ, cras_tm_create_timer_called, 0);
    CLEAR_AND_EVENTUALLY(EXPECT_EQ, audio_thread_add_stream_called, 0);

    cras_iodev_open_ret[cras_iodev_open_called] = -5;

    cras_tm_timer_cb(NULL, cras_tm_timer_cb_data);
  }

  {
    CLEAR_AND_EVENTUALLY(EXPECT_EQ, cras_tm_create_timer_called, 1);
    CLEAR_AND_EVENTUALLY(EXPECT_NE, cras_tm_timer_cb, nullptr);

    cras_iodev_open_ret[cras_iodev_open_called] = -5;

    stream_add_cb(&rstream);
  }

  {
    CLEAR_AND_EVENTUALLY(EXPECT_EQ, cras_tm_cancel_timer_called, 1);

    cras_iodev_list_rm_output(&d1_);
  }

  cras_iodev_list_deinit();
}

TEST_F(IoDevTestSuite, PinnedStreamInitFailShouldScheduleRetry) {
  int rc;
  struct cras_rstream rstream;

  memset(&rstream, 0, sizeof(rstream));
  cras_iodev_list_init();

  d1_.direction = CRAS_STREAM_OUTPUT;
  rc = cras_iodev_list_add_output(&d1_);
  ASSERT_EQ(rc, 0);

  d1_.format = &fmt_;

  rstream.is_pinned = 1;
  rstream.pinned_dev_idx = d1_.info.idx;

  {  // Init pinned dev failed, thus not adding stream.
    CLEAR_AND_EVENTUALLY(EXPECT_EQ, cras_iodev_open_called, 1);
    CLEAR_AND_EVENTUALLY(EXPECT_EQ, audio_thread_add_stream_called, 0);
    CLEAR_AND_EVENTUALLY(EXPECT_EQ, cras_tm_create_timer_called, 1);
    CLEAR_AND_EVENTUALLY(EXPECT_NE, cras_tm_timer_cb, nullptr);

    cras_iodev_open_ret[cras_iodev_open_called] = -5;

    DL_APPEND(stream_list_get_ret, &rstream);
    stream_add_cb(&rstream);
  }

  {
    CLEAR_AND_EVENTUALLY(EXPECT_EQ, audio_thread_add_stream_called, 1);

    const int prev_cras_iodev_open_called = cras_iodev_open_called;

    EVENTUALLY(EXPECT_EQ, 1,
               cras_iodev_open_called - prev_cras_iodev_open_called);

    cras_tm_timer_cb(NULL, cras_tm_timer_cb_data);
  }

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
  ASSERT_EQ(rc, 0);

  d2_.direction = CRAS_STREAM_OUTPUT;
  node2.idx = 2;
  rc = cras_iodev_list_add_output(&d2_);
  ASSERT_EQ(rc, 0);

  d1_.format = &fmt_;
  d2_.format = &fmt_;

  {
    CLEAR_AND_EVENTUALLY(EXPECT_EQ, device_enabled_count, 1);
    EVENTUALLY(EXPECT_EQ,
               cras_iodev_list_get_first_enabled_iodev(CRAS_STREAM_OUTPUT),
               &d1_);
    CLEAR_AND_EVENTUALLY(EXPECT_EQ, device_disabled_count, 1);
    CLEAR_AND_EVENTUALLY(EXPECT_EQ, audio_thread_rm_open_dev_called, 0);
    CLEAR_AND_EVENTUALLY(EXPECT_EQ, cras_observer_notify_active_node_called, 1);

    // No close call actually happened, because no stream exists.
    CLEAR_AND_EVENTUALLY(EXPECT_EQ, cras_iodev_close_called, 0);

    // There should be a disable device call for the fallback device.
    CLEAR_AND_EVENTUALLY(EXPECT_EQ, device_disabled_dev,
                         fallback_devs[CRAS_STREAM_OUTPUT]);

    cras_iodev_list_set_device_enabled_callback(
        device_enabled_cb, device_disabled_cb, NULL, (void*)0xABCD);

    cras_iodev_list_add_active_node(CRAS_STREAM_OUTPUT,
                                    cras_make_node_id(d1_.info.idx, 1));
  }

  {
    CLEAR_AND_EVENTUALLY(EXPECT_EQ, audio_thread_add_stream_called, 1);
    CLEAR_AND_EVENTUALLY(EXPECT_EQ, audio_thread_add_open_dev_called, 1);

    DL_APPEND(stream_list_get_ret, &rstream);
    stream_add_cb(&rstream);
  }

  {
    CLEAR_AND_EVENTUALLY(EXPECT_EQ, audio_thread_add_stream_called, 1);
    CLEAR_AND_EVENTUALLY(EXPECT_EQ, audio_thread_add_open_dev_called, 0);

    DL_APPEND(stream_list_get_ret, &rstream2);
    stream_add_cb(&rstream2);
  }

  {
    CLEAR_AND_EVENTUALLY(EXPECT_EQ, cras_observer_notify_active_node_called, 1);

    // Additional enabled devices: fallback device, d2_.
    CLEAR_AND_EVENTUALLY(EXPECT_EQ, device_enabled_count, 2);

    // For each stream, the stream is added for fallback device and d2_.
    CLEAR_AND_EVENTUALLY(EXPECT_EQ, audio_thread_add_stream_called, 2);
    CLEAR_AND_EVENTUALLY(EXPECT_EQ, audio_thread_add_stream_fallback_called, 2);

    // Additional disabled devices: d1_, fallback device.
    CLEAR_AND_EVENTUALLY(EXPECT_EQ, device_disabled_count, 2);
    CLEAR_AND_EVENTUALLY(EXPECT_EQ, audio_thread_rm_open_dev_called, 1);
    CLEAR_AND_EVENTUALLY(EXPECT_EQ, audio_thread_rm_open_dev_fallback_called,
                         1);

    EVENTUALLY(EXPECT_EQ,
               cras_iodev_list_get_first_enabled_iodev(CRAS_STREAM_OUTPUT),
               &d2_);

    cras_iodev_list_select_node(CRAS_STREAM_OUTPUT,
                                cras_make_node_id(d2_.info.idx, 2));
  }

  cras_iodev_list_set_device_enabled_callback(NULL, NULL, NULL, NULL);

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
  ASSERT_EQ(rc, 0);

  d2_.direction = CRAS_STREAM_OUTPUT;
  node2.idx = 2;
  rc = cras_iodev_list_add_output(&d2_);
  ASSERT_EQ(rc, 0);

  d1_.format = &fmt_;
  d2_.format = &fmt_;

  {
    CLEAR_AND_EVENTUALLY(EXPECT_EQ, device_enabled_count, 1);
    EVENTUALLY(EXPECT_EQ,
               cras_iodev_list_get_first_enabled_iodev(CRAS_STREAM_OUTPUT),
               &d1_);

    CLEAR_AND_EVENTUALLY(EXPECT_EQ, device_disabled_count, 1);
    CLEAR_AND_EVENTUALLY(EXPECT_EQ, device_disabled_dev,
                         fallback_devs[CRAS_STREAM_OUTPUT]);

    CLEAR_AND_EVENTUALLY(EXPECT_EQ, cras_observer_notify_active_node_called, 1);

    cras_iodev_list_set_device_enabled_callback(
        device_enabled_cb, device_disabled_cb, NULL, (void*)0xABCD);

    cras_iodev_list_add_active_node(CRAS_STREAM_OUTPUT,
                                    cras_make_node_id(d1_.info.idx, 1));
  }

  {
    CLEAR_AND_EVENTUALLY(EXPECT_EQ, audio_thread_add_open_dev_called, 1);
    CLEAR_AND_EVENTUALLY(EXPECT_EQ, audio_thread_add_stream_called, 1);

    DL_APPEND(stream_list_get_ret, &rstream);
    stream_add_cb(&rstream);
  }

  {  // Add a second active node.
    CLEAR_AND_EVENTUALLY(EXPECT_EQ, device_enabled_count, 1);
    CLEAR_AND_EVENTUALLY(EXPECT_EQ, device_disabled_count, 0);
    CLEAR_AND_EVENTUALLY(EXPECT_EQ, audio_thread_add_open_dev_called, 1);
    CLEAR_AND_EVENTUALLY(EXPECT_EQ, audio_thread_rm_open_dev_called, 0);
    CLEAR_AND_EVENTUALLY(EXPECT_EQ, audio_thread_add_stream_called, 1);
    CLEAR_AND_EVENTUALLY(EXPECT_EQ, cras_observer_notify_active_node_called, 1);

    EVENTUALLY(EXPECT_EQ,
               cras_iodev_list_get_first_enabled_iodev(CRAS_STREAM_OUTPUT),
               &d1_);

    cras_iodev_list_add_active_node(CRAS_STREAM_OUTPUT,
                                    cras_make_node_id(d2_.info.idx, 2));
  }

  {  // Select the second added active node - should disable first node
    CLEAR_AND_EVENTUALLY(EXPECT_EQ, device_enabled_count, 0);
    CLEAR_AND_EVENTUALLY(EXPECT_EQ, device_disabled_count, 1);
    CLEAR_AND_EVENTUALLY(EXPECT_EQ, device_disabled_dev, &d1_);
    CLEAR_AND_EVENTUALLY(EXPECT_EQ, audio_thread_add_stream_called, 0);
    CLEAR_AND_EVENTUALLY(EXPECT_EQ, audio_thread_add_open_dev_called, 0);
    CLEAR_AND_EVENTUALLY(EXPECT_EQ, audio_thread_rm_open_dev_called, 1);
    CLEAR_AND_EVENTUALLY(EXPECT_EQ, cras_observer_notify_active_node_called, 1);
    EVENTUALLY(EXPECT_EQ,
               cras_iodev_list_get_first_enabled_iodev(CRAS_STREAM_OUTPUT),
               &d2_);

    cras_iodev_list_select_node(CRAS_STREAM_OUTPUT,
                                cras_make_node_id(d2_.info.idx, 2));
  }

  cras_iodev_list_set_device_enabled_callback(NULL, NULL, NULL, NULL);

  cras_iodev_list_deinit();
}

TEST_F(IoDevTestSuite, UpdateActiveNode) {
  int rc;

  cras_iodev_list_init();

  d1_.direction = CRAS_STREAM_OUTPUT;
  rc = cras_iodev_list_add_output(&d1_);
  ASSERT_EQ(rc, 0);

  d2_.direction = CRAS_STREAM_OUTPUT;
  rc = cras_iodev_list_add_output(&d2_);
  ASSERT_EQ(rc, 0);

  {
    CLEAR_AND_EVENTUALLY(EXPECT_EQ, update_active_node_called, 2);
    CLEAR_AND_EVENTUALLY(EXPECT_EQ, cras_observer_notify_active_node_called, 1);
    EVENTUALLY(EXPECT_EQ, update_active_node_iodev_val[0], &d2_);
    EVENTUALLY(EXPECT_EQ, update_active_node_node_idx_val[0], 1);
    EVENTUALLY(EXPECT_EQ, update_active_node_dev_enabled_val[0], 1);

    cras_iodev_list_select_node(CRAS_STREAM_OUTPUT,
                                cras_make_node_id(d2_.info.idx, 1));
  }

  { /* Fake the active node idx on d2_, and later assert this node is
     * called for update_active_node when d2_ disabled. */
    CLEAR_AND_EVENTUALLY(EXPECT_EQ, update_active_node_called, 3);
    CLEAR_AND_EVENTUALLY(EXPECT_EQ, cras_observer_notify_active_node_called, 1);

    const int fake_idx = MAX_SPECIAL_DEVICE_IDX;

    EVENTUALLY(EXPECT_EQ, update_active_node_iodev_val[0], &d2_);
    EVENTUALLY(EXPECT_EQ, update_active_node_node_idx_val[0], fake_idx);
    EVENTUALLY(EXPECT_EQ, update_active_node_dev_enabled_val[0], 0);

    EVENTUALLY(EXPECT_EQ, update_active_node_iodev_val[1], &d1_);
    EVENTUALLY(EXPECT_EQ, update_active_node_node_idx_val[1], 0);
    EVENTUALLY(EXPECT_EQ, update_active_node_dev_enabled_val[1], 1);

    d2_.active_node->idx = fake_idx;

    cras_iodev_list_select_node(CRAS_STREAM_OUTPUT,
                                cras_make_node_id(d1_.info.idx, 0));
  }

  cras_iodev_list_deinit();
}

TEST_F(IoDevTestSuite, SelectNonExistingNode) {
  int rc;
  cras_iodev_list_init();

  d1_.direction = CRAS_STREAM_OUTPUT;
  rc = cras_iodev_list_add_output(&d1_);
  ASSERT_EQ(rc, 0);

  {
    CLEAR_AND_EVENTUALLY(EXPECT_EQ, cras_observer_notify_active_node_called, 1);
    EVENTUALLY(EXPECT_EQ, d1_.is_enabled, 1);

    cras_iodev_list_select_node(CRAS_STREAM_OUTPUT,
                                cras_make_node_id(d1_.info.idx, 0));
  }

  {  // Select non-existing node should disable all devices.
    CLEAR_AND_EVENTUALLY(EXPECT_EQ, cras_observer_notify_active_node_called, 1);
    EVENTUALLY(EXPECT_EQ, d1_.is_enabled, 0);

    cras_iodev_list_select_node(CRAS_STREAM_OUTPUT, cras_make_node_id(2, 1));
  }

  cras_iodev_list_deinit();
}

// Devices with the wrong direction should be rejected.
TEST_F(IoDevTestSuite, AddWrongDirection) {
  int rc;

  rc = cras_iodev_list_add_input(&d1_);
  EXPECT_EQ(rc, -EINVAL);

  d1_.direction = CRAS_STREAM_INPUT;

  rc = cras_iodev_list_add_output(&d1_);
  EXPECT_EQ(rc, -EINVAL);
}

// Test adding/removing an iodev to the list.
TEST_F(IoDevTestSuite, AddRemoveOutput) {
  struct cras_iodev_info* dev_info;
  int rc;

  cras_iodev_list_init();

  rc = cras_iodev_list_add_output(&d1_);
  EXPECT_EQ(rc, 0);

  // Test can't insert same iodev twice.
  rc = cras_iodev_list_add_output(&d1_);
  EXPECT_NE(rc, 0);

  // Test insert a second output.
  rc = cras_iodev_list_add_output(&d2_);
  EXPECT_EQ(rc, 0);

  // Test that it is removed.
  rc = cras_iodev_list_rm_output(&d1_);
  EXPECT_EQ(rc, 0);

  // Test that we can't remove a dev twice.
  rc = cras_iodev_list_rm_output(&d1_);
  EXPECT_NE(rc, 0);

  // Should be 1 dev now.
  EXPECT_EQ(cras_iodev_list_get_outputs(&dev_info), 1);
  free(dev_info);

  // Passing null should return the number of outputs.
  EXPECT_EQ(cras_iodev_list_get_outputs(NULL), 1);

  // Remove other dev.
  rc = cras_iodev_list_rm_output(&d2_);
  EXPECT_EQ(rc, 0);

  // Should be 0 devs now.
  EXPECT_EQ(cras_iodev_list_get_outputs(&dev_info), 0);
  free(dev_info);

  EXPECT_EQ(cras_observer_notify_active_node_called, 0);

  cras_iodev_list_deinit();
}

// Test output_mute_changed callback.
TEST_F(IoDevTestSuite, OutputMuteChangedToMute) {
  cras_iodev_list_init();

  d1_.info.idx = 1;
  d2_.info.idx = 2;

  cras_iodev_list_add_output(&d1_);
  cras_iodev_list_add_output(&d2_);
  cras_iodev_list_add_output(&d3_);

  // d1_ and d2_ are enabled.
  cras_iodev_list_select_node(CRAS_STREAM_OUTPUT,
                              cras_make_node_id(d1_.info.idx, 0));
  cras_iodev_list_add_active_node(CRAS_STREAM_OUTPUT,
                                  cras_make_node_id(d2_.info.idx, 0));

  // Assume d1 and d2 devices are open.
  d1_.state = CRAS_IODEV_STATE_OPEN;
  d2_.state = CRAS_IODEV_STATE_OPEN;
  d3_.state = CRAS_IODEV_STATE_CLOSE;

  // Execute the callback.
  observer_ops->output_mute_changed(NULL, 0, 1, 0);

  // d1_ and d2_ should set mute state through audio_thread_dev_start_ramp
  // because they are both open.
  EXPECT_EQ(audio_thread_dev_start_ramp_called, 2);
  ASSERT_TRUE(
      dev_idx_in_vector(audio_thread_dev_start_ramp_dev_vector, d2_.info.idx));
  ASSERT_TRUE(
      dev_idx_in_vector(audio_thread_dev_start_ramp_dev_vector, d1_.info.idx));
  EXPECT_EQ(audio_thread_dev_start_ramp_req, CRAS_IODEV_RAMP_REQUEST_DOWN_MUTE);

  // d3_ should set mute state right away without calling ramp
  // because it is not open.
  EXPECT_EQ(set_mute_called, 1);
  EXPECT_EQ(set_mute_dev_vector.size(), 1);
  ASSERT_TRUE(device_in_vector(set_mute_dev_vector, &d3_));

  cras_iodev_list_deinit();
}

// Test output_mute_changed callback.
TEST_F(IoDevTestSuite, OutputMuteChangedToUnmute) {
  cras_iodev_list_init();

  d1_.info.idx = 1;
  d2_.info.idx = 2;
  cras_iodev_list_add_output(&d1_);
  cras_iodev_list_add_output(&d2_);
  cras_iodev_list_add_output(&d3_);

  // d1_ and d2_ are enabled.
  cras_iodev_list_select_node(CRAS_STREAM_OUTPUT,
                              cras_make_node_id(d1_.info.idx, 0));
  cras_iodev_list_add_active_node(CRAS_STREAM_OUTPUT,
                                  cras_make_node_id(d2_.info.idx, 0));

  // Assume d1 and d2 devices are open.
  d1_.state = CRAS_IODEV_STATE_OPEN;
  d2_.state = CRAS_IODEV_STATE_CLOSE;
  d3_.state = CRAS_IODEV_STATE_CLOSE;

  // Execute the callback.
  observer_ops->output_mute_changed(NULL, 0, 0, 0);

  // d1_ should set mute state through audio_thread_dev_start_ramp.
  EXPECT_EQ(audio_thread_dev_start_ramp_called, 1);
  ASSERT_TRUE(
      dev_idx_in_vector(audio_thread_dev_start_ramp_dev_vector, d1_.info.idx));
  EXPECT_EQ(audio_thread_dev_start_ramp_req, CRAS_IODEV_RAMP_REQUEST_UP_UNMUTE);

  // d2_ and d3_ should set mute state right away because they both
  // are closed.
  EXPECT_EQ(set_mute_called, 2);
  EXPECT_EQ(set_mute_dev_vector.size(), 2);
  ASSERT_TRUE(device_in_vector(set_mute_dev_vector, &d2_));
  ASSERT_TRUE(device_in_vector(set_mute_dev_vector, &d3_));

  cras_iodev_list_deinit();
}

// Test enable/disable an iodev.
TEST_F(IoDevTestSuite, EnableDisableDevice) {
  struct cras_rstream rstream;
  int rc;

  memset(&rstream, 0, sizeof(rstream));

  cras_iodev_list_init();

  d1_.info.idx = 1;
  rc = cras_iodev_list_add_output(&d1_);
  EXPECT_EQ(rc, 0);

  cras_iodev_list_set_device_enabled_callback(
      device_enabled_cb, device_disabled_cb, NULL, (void*)0xABCD);

  {  // Enable a device, fallback should be disabled accordingly.
    CLEAR_AND_EVENTUALLY(EXPECT_EQ, device_enabled_count, 1);
    CLEAR_AND_EVENTUALLY(EXPECT_EQ, device_enabled_dev, &d1_);
    EVENTUALLY(EXPECT_EQ,
               cras_iodev_list_get_first_enabled_iodev(CRAS_STREAM_OUTPUT),
               &d1_);
    CLEAR_AND_EVENTUALLY(EXPECT_EQ, device_disabled_count, 1);
    CLEAR_AND_EVENTUALLY(EXPECT_EQ, device_enabled_cb_data, (void*)0xABCD);
    CLEAR_AND_EVENTUALLY(EXPECT_EQ, cras_observer_notify_active_node_called, 1);

    cras_iodev_list_select_node(CRAS_STREAM_OUTPUT,
                                cras_make_node_id(d1_.info.idx, 0));
  }

  {  // Connect a normal stream.
    CLEAR_AND_EVENTUALLY(EXPECT_EQ, cras_iodev_open_called, 1);

    DL_APPEND(stream_list_get_ret, &rstream);
    stream_add_cb(&rstream);
  }

  {  // Disable a device. Expect dev is closed because there's no pinned stream.
    CLEAR_AND_EVENTUALLY(EXPECT_EQ, audio_thread_rm_open_dev_called, 1);
    CLEAR_AND_EVENTUALLY(EXPECT_EQ, device_disabled_count, 1);
    CLEAR_AND_EVENTUALLY(EXPECT_EQ, device_disabled_dev, &d1_);
    CLEAR_AND_EVENTUALLY(EXPECT_EQ, device_disabled_cb_data, (void*)0xABCD);
    CLEAR_AND_EVENTUALLY(EXPECT_EQ, cras_iodev_close_called, 1);
    CLEAR_AND_EVENTUALLY(EXPECT_EQ, cras_iodev_close_dev, &d1_);
    CLEAR_AND_EVENTUALLY(EXPECT_EQ, update_active_node_called, 1);
    CLEAR_AND_EVENTUALLY(EXPECT_EQ, cras_observer_notify_active_node_called, 1);

    cras_iodev_list_disable_dev(&d1_, false);
  }

  cras_iodev_list_set_device_enabled_callback(
      device_enabled_cb, device_disabled_cb, NULL, (void*)0xCDEF);

  cras_iodev_list_set_device_enabled_callback(NULL, NULL, NULL, NULL);

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
  EXPECT_EQ(rc, 0);

  rc = cras_iodev_list_add_input(&d1_);
  EXPECT_EQ(rc, 0);
  EXPECT_GE(d1_.info.idx, 0);
  // Test can't insert same iodev twice.
  rc = cras_iodev_list_add_input(&d1_);
  EXPECT_NE(rc, 0);
  // Test insert a second input.
  rc = cras_iodev_list_add_input(&d2_);
  EXPECT_EQ(rc, 0);
  EXPECT_GE(d2_.info.idx, 1);
  // make sure shared state was updated.
  EXPECT_EQ(server_state_stub.num_input_devs, 2);
  EXPECT_EQ(server_state_stub.input_devs[0].idx, d2_.info.idx);
  EXPECT_EQ(server_state_stub.input_devs[1].idx, d1_.info.idx);

  // List the outputs.
  rc = cras_iodev_list_get_inputs(&dev_info);
  EXPECT_EQ(rc, 2);
  if (rc == 2) {
    found_mask = 0;
    for (i = 0; i < rc; i++) {
      uint32_t idx = dev_info[i].idx;
      EXPECT_EQ((found_mask & (static_cast<uint64_t>(1) << idx)), 0);
      found_mask |= (static_cast<uint64_t>(1) << idx);
    }
  }
  if (rc > 0) {
    free(dev_info);
  }

  // Test that it is removed.
  rc = cras_iodev_list_rm_input(&d1_);
  EXPECT_EQ(rc, 0);
  // Test that we can't remove a dev twice.
  rc = cras_iodev_list_rm_input(&d1_);
  EXPECT_NE(rc, 0);
  // Should be 1 dev now.
  rc = cras_iodev_list_get_inputs(&dev_info);
  EXPECT_EQ(rc, 1);
  free(dev_info);
  // Remove other dev.
  rc = cras_iodev_list_rm_input(&d2_);
  EXPECT_EQ(rc, 0);
  // Shouldn't be any devices left.
  rc = cras_iodev_list_get_inputs(&dev_info);
  EXPECT_EQ(rc, 0);
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
  EXPECT_EQ(rc, 0);
  EXPECT_GE(d1_.info.idx, 0);
  rc = cras_iodev_list_add_input(&d2_);
  EXPECT_EQ(rc, 0);
  EXPECT_GE(d2_.info.idx, 1);

  EXPECT_EQ(cras_iodev_list_rm_input(&d1_), 0);
  EXPECT_EQ(cras_iodev_list_rm_input(&d2_), 0);
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
  EXPECT_EQ(rc, 0);
  rc = cras_iodev_list_add_input(&d2_);
  EXPECT_EQ(rc, 0);

  // Test that it is removed.
  rc = cras_iodev_list_rm_input(&d1_);
  EXPECT_EQ(rc, 0);
  // Add it back.
  rc = cras_iodev_list_add_input(&d1_);
  EXPECT_EQ(rc, 0);
  // And again.
  rc = cras_iodev_list_rm_input(&d1_);
  EXPECT_EQ(rc, 0);
  // Add it back.
  rc = cras_iodev_list_add_input(&d1_);
  EXPECT_EQ(rc, 0);
  // Remove other dev.
  rc = cras_iodev_list_rm_input(&d2_);
  EXPECT_EQ(rc, 0);
  // Add it back.
  rc = cras_iodev_list_add_input(&d2_);
  EXPECT_EQ(rc, 0);
  // Remove both.
  rc = cras_iodev_list_rm_input(&d2_);
  EXPECT_EQ(rc, 0);
  rc = cras_iodev_list_rm_input(&d1_);
  EXPECT_EQ(rc, 0);
  // Shouldn't be any devices left.
  rc = cras_iodev_list_get_inputs(&dev_info);
  EXPECT_EQ(rc, 0);

  cras_iodev_list_deinit();
}

// Test nodes changed notification is sent.
TEST_F(IoDevTestSuite, NodesChangedNotification) {
  cras_iodev_list_init();
  EXPECT_EQ(cras_observer_add_called, 1);

  cras_iodev_list_notify_nodes_changed();
  EXPECT_EQ(cras_observer_notify_nodes_called, 1);

  cras_iodev_list_deinit();
  EXPECT_EQ(cras_observer_remove_called, 1);
}

// Test callback function for left right swap mode is set and called.
TEST_F(IoDevTestSuite, NodesLeftRightSwappedCallback) {
  struct cras_iodev iodev;
  struct cras_ionode ionode;
  memset(&iodev, 0, sizeof(iodev));
  memset(&ionode, 0, sizeof(ionode));
  ionode.dev = &iodev;
  cras_iodev_list_notify_node_left_right_swapped(&ionode);
  EXPECT_EQ(cras_observer_notify_node_left_right_swapped_called, 1);
}

// Test callback function for volume and gain are set and called.
TEST_F(IoDevTestSuite, VolumeGainCallback) {
  struct cras_iodev iodev;
  struct cras_ionode ionode;
  memset(&iodev, 0, sizeof(iodev));
  memset(&ionode, 0, sizeof(ionode));
  ionode.dev = &iodev;
  cras_iodev_list_notify_node_volume(&ionode);
  int capture_gain = 50;
  cras_iodev_list_notify_node_capture_gain(&ionode, capture_gain);
  EXPECT_EQ(cras_observer_notify_output_node_volume_called, 1);
  EXPECT_EQ(cras_observer_notify_input_node_gain_called, 1);
  EXPECT_EQ(cras_observer_notify_input_node_gain_value, 50);
}

TEST_F(IoDevTestSuite, IodevListSetNodeAttr) {
  int rc;

  cras_iodev_list_init();

  // The list is empty now.
  rc = cras_iodev_list_set_node_attr(cras_make_node_id(0, 0),
                                     IONODE_ATTR_PLUGGED, 1);
  EXPECT_LT(rc, 0);
  EXPECT_EQ(set_node_plugged_called, 0);

  // Add two device, each with one node.
  d1_.direction = CRAS_STREAM_INPUT;
  EXPECT_EQ(cras_iodev_list_add_input(&d1_), 0);
  node1.idx = 1;
  EXPECT_EQ(cras_iodev_list_add_output(&d2_), 0);
  node2.idx = 2;

  // Mismatch id
  rc = cras_iodev_list_set_node_attr(cras_make_node_id(d2_.info.idx, 1),
                                     IONODE_ATTR_PLUGGED, 1);
  EXPECT_LT(rc, 0);
  EXPECT_EQ(set_node_plugged_called, 0);

  // Mismatch id
  rc = cras_iodev_list_set_node_attr(cras_make_node_id(d1_.info.idx, 2),
                                     IONODE_ATTR_PLUGGED, 1);
  EXPECT_LT(rc, 0);
  EXPECT_EQ(set_node_plugged_called, 0);

  // Correct device id and node id
  rc = cras_iodev_list_set_node_attr(cras_make_node_id(d1_.info.idx, 1),
                                     IONODE_ATTR_PLUGGED, 1);
  EXPECT_EQ(0, rc);
  EXPECT_EQ(set_node_plugged_called, 1);
  cras_iodev_list_deinit();
}

TEST_F(IoDevTestSuite, SetNodeVolumeCaptureGain) {
  int rc;

  cras_iodev_list_init();

  d1_.direction = CRAS_STREAM_OUTPUT;
  rc = cras_iodev_list_add_output(&d1_);
  ASSERT_EQ(rc, 0);
  node1.idx = 1;
  node1.dev = &d1_;

  // Do not ramp without software volume.
  d1_.software_volume_needed = 0;
  cras_iodev_list_set_node_attr(cras_make_node_id(d1_.info.idx, 1),
                                IONODE_ATTR_VOLUME, 10);
  EXPECT_EQ(cras_observer_notify_output_node_volume_called, 1);
  EXPECT_EQ(cras_iodev_start_volume_ramp_called, 0);

  // Even with software volume, device with NULL ramp won't trigger ramp start.
  d1_.software_volume_needed = 1;
  cras_iodev_list_set_node_attr(cras_make_node_id(d1_.info.idx, 1),
                                IONODE_ATTR_VOLUME, 20);
  EXPECT_EQ(cras_observer_notify_output_node_volume_called, 2);
  EXPECT_EQ(cras_iodev_start_volume_ramp_called, 0);

  // System mute prevents volume ramp from starting
  system_get_mute_return = true;
  cras_iodev_list_set_node_attr(cras_make_node_id(d1_.info.idx, 1),
                                IONODE_ATTR_VOLUME, 20);
  EXPECT_EQ(cras_observer_notify_output_node_volume_called, 3);
  EXPECT_EQ(cras_iodev_start_volume_ramp_called, 0);

  // Ramp starts only when it's non-NULL, software volume is used, and
  // system is not muted
  system_get_mute_return = false;
  d1_.ramp = reinterpret_cast<struct cras_ramp*>(0x1);
  cras_iodev_list_set_node_attr(cras_make_node_id(d1_.info.idx, 1),
                                IONODE_ATTR_VOLUME, 20);
  EXPECT_EQ(cras_observer_notify_output_node_volume_called, 4);
  EXPECT_EQ(cras_iodev_start_volume_ramp_called, 1);

  d1_.direction = CRAS_STREAM_INPUT;
  cras_iodev_list_set_node_attr(cras_make_node_id(d1_.info.idx, 1),
                                IONODE_ATTR_CAPTURE_GAIN, 15);
  EXPECT_EQ(cras_observer_notify_input_node_gain_called, 1);
  cras_iodev_list_deinit();
}

TEST_F(IoDevTestSuite, SetNodeVolumeInvalid) {
  int rc;

  cras_iodev_list_init();

  d1_.direction = CRAS_STREAM_OUTPUT;
  rc = cras_iodev_list_add_output(&d1_);
  ASSERT_EQ(rc, 0);
  node1.idx = 1;
  node1.dev = &d1_;
  const int wrong_id = -1;

  d1_.software_volume_needed = 0;
  rc = cras_iodev_list_set_node_attr(cras_make_node_id(d1_.info.idx, 1),
                                     IONODE_ATTR_VOLUME, -1);
  EXPECT_EQ(rc, -EINVAL);
  EXPECT_EQ(cras_observer_notify_output_node_volume_called, 0);

  rc = cras_iodev_list_set_node_attr(cras_make_node_id(d1_.info.idx, 1),
                                     IONODE_ATTR_VOLUME, 101);
  EXPECT_EQ(rc, -EINVAL);
  EXPECT_EQ(cras_observer_notify_output_node_volume_called, 0);

  rc = cras_iodev_list_set_node_attr(wrong_id, IONODE_ATTR_VOLUME, 50);
  EXPECT_EQ(rc, -EINVAL);
  EXPECT_EQ(cras_observer_notify_output_node_volume_called, 0);

  cras_iodev_list_deinit();
}

TEST_F(IoDevTestSuite, SetNodeSwapLeftRight) {
  int rc;

  cras_iodev_list_init();

  rc = cras_iodev_list_add_output(&d1_);
  ASSERT_EQ(rc, 0);
  node1.idx = 1;
  node1.dev = &d1_;

  cras_iodev_list_set_node_attr(cras_make_node_id(d1_.info.idx, 1),
                                IONODE_ATTR_SWAP_LEFT_RIGHT, 1);
  EXPECT_EQ(set_swap_mode_for_node_called, 1);
  EXPECT_EQ(set_swap_mode_for_node_enable, 1);
  EXPECT_EQ(node1.left_right_swapped, 1);
  EXPECT_EQ(cras_observer_notify_node_left_right_swapped_called, 1);

  cras_iodev_list_set_node_attr(cras_make_node_id(d1_.info.idx, 1),
                                IONODE_ATTR_SWAP_LEFT_RIGHT, 0);
  EXPECT_EQ(set_swap_mode_for_node_called, 2);
  EXPECT_EQ(set_swap_mode_for_node_enable, 0);
  EXPECT_EQ(node1.left_right_swapped, 0);
  EXPECT_EQ(cras_observer_notify_node_left_right_swapped_called, 2);
  cras_iodev_list_deinit();
}

TEST_F(IoDevTestSuite, SetNodeDisplayRotation) {
  int rc;
  cras_iodev_list_init();

  rc = cras_iodev_list_add_output(&d1_);
  ASSERT_EQ(rc, 0);
  node1.idx = 1;
  node1.dev = &d1_;

  cras_iodev_list_set_node_attr(cras_make_node_id(d1_.info.idx, 1),
                                IONODE_ATTR_DISPLAY_ROTATION, ROTATE_180);
  EXPECT_EQ(set_display_rotation_for_node_called, 1);
  EXPECT_EQ(display_rotation, ROTATE_180);
  EXPECT_EQ(node1.display_rotation, ROTATE_180);

  cras_iodev_list_set_node_attr(cras_make_node_id(d1_.info.idx, 1),
                                IONODE_ATTR_DISPLAY_ROTATION, ROTATE_270);
  EXPECT_EQ(set_display_rotation_for_node_called, 2);
  EXPECT_EQ(display_rotation, ROTATE_270);
  EXPECT_EQ(node1.display_rotation, ROTATE_270);
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
  ASSERT_EQ(rc, 0);
  rc = cras_iodev_list_add_output(&d2_);
  ASSERT_EQ(rc, 0);
  rc = cras_iodev_list_add_output(&d3_);
  ASSERT_EQ(rc, 0);

  d1_.format = &fmt_;
  d2_.format = &fmt_;
  d3_.format = &fmt_;

  {
    CLEAR_AND_EVENTUALLY(EXPECT_EQ, audio_thread_add_open_dev_called, 0);
    CLEAR_AND_EVENTUALLY(EXPECT_EQ, audio_thread_rm_open_dev_called, 0);

    cras_iodev_list_add_active_node(CRAS_STREAM_OUTPUT,
                                    cras_make_node_id(d3_.info.idx, 1));
  }

  {  // If a stream is added, the device should be opened.
    CLEAR_AND_EVENTUALLY(EXPECT_EQ, audio_thread_add_open_dev_called, 1);

    DL_APPEND(stream_list_get_ret, &rstream);
    stream_add_cb(&rstream);
  }

  {  // If drain > 0, do not remove yet
    CLEAR_AND_EVENTUALLY(EXPECT_EQ, audio_thread_drain_stream_called, 1);
    CLEAR_AND_EVENTUALLY(EXPECT_EQ, audio_thread_rm_open_dev_called, 0);

    audio_thread_drain_stream_return = 10;

    EVENTUALLY(EXPECT_EQ, audio_thread_drain_stream_return, rc);

    DL_DELETE(stream_list_get_ret, &rstream);
    rc = stream_rm_cb(&rstream);
  }

  {  // Stream should remain open for a while before being closed.
    CLEAR_AND_EVENTUALLY(EXPECT_EQ, audio_thread_drain_stream_called, 1);
    CLEAR_AND_EVENTUALLY(EXPECT_EQ, audio_thread_rm_open_dev_called, 0);

    audio_thread_drain_stream_return = 0;

    EVENTUALLY(EXPECT_EQ, rc, 0);

    rc = stream_rm_cb(&rstream);
  }

  {  // Test it is closed strictly after idle_timeout_interval(=10) seconds.
    CLEAR_AND_EVENTUALLY(EXPECT_EQ, audio_thread_rm_open_dev_called, 1);

    clock_gettime_retspec.tv_sec += 10;
    clock_gettime_retspec.tv_nsec += 1;

    cras_tm_timer_cb(NULL, NULL);
  }

  {
    CLEAR_AND_EVENTUALLY(EXPECT_EQ, audio_thread_rm_open_dev_called, 0);

    cras_iodev_list_rm_output(&d3_);
  }

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
  EXPECT_EQ(rc, 0);

  d1_.format = &fmt_;

  {
    CLEAR_AND_EVENTUALLY(EXPECT_EQ, audio_thread_add_open_dev_called, 0);
    CLEAR_AND_EVENTUALLY(EXPECT_EQ, audio_thread_rm_open_dev_called, 0);

    cras_iodev_list_add_active_node(CRAS_STREAM_OUTPUT,
                                    cras_make_node_id(d1_.info.idx, 1));
  }

  {  // If a stream is added, the device should be opened.
    CLEAR_AND_EVENTUALLY(EXPECT_EQ, audio_thread_add_open_dev_called, 1);

    DL_APPEND(stream_list_get_ret, &rstream);
    stream_add_cb(&rstream);
  }

  {  // Before actually removing, it will create a timer for
     // idle_timeout_interval(=10)s
    CLEAR_AND_EVENTUALLY(EXPECT_EQ, audio_thread_drain_stream_called, 1);
    CLEAR_AND_EVENTUALLY(EXPECT_EQ, audio_thread_rm_open_dev_called, 0);
    CLEAR_AND_EVENTUALLY(EXPECT_EQ, cras_tm_create_timer_called, 1);

    DL_DELETE(stream_list_get_ret, &rstream);
    stream_rm_cb(&rstream);
  }

  {  // Expect no rm dev happen because idle time not yet expire, and
     // new timer should be scheduled for the rest of the idle time.
    CLEAR_AND_EVENTUALLY(EXPECT_EQ, audio_thread_rm_open_dev_called, 0);
    CLEAR_AND_EVENTUALLY(EXPECT_EQ, cras_tm_create_timer_called, 1);

    clock_gettime_retspec.tv_sec += 7;

    cras_tm_timer_cb(NULL, NULL);
  }

  {  // Expect d1_ be closed upon unplug, and the timer stay armed.
    CLEAR_AND_EVENTUALLY(EXPECT_EQ, audio_thread_rm_open_dev_called, 1);
    CLEAR_AND_EVENTUALLY(EXPECT_EQ, cras_tm_cancel_timer_called, 0);

    cras_iodev_list_rm_output(&d1_);
  }

  {  // When timer eventually fired expect there's no more new
     // timer scheduled because d1_ has closed already.
    CLEAR_AND_EVENTUALLY(EXPECT_EQ, cras_tm_create_timer_called, 0);

    clock_gettime_retspec.tv_sec += 4;

    cras_tm_timer_cb(NULL, NULL);
  }

  cras_iodev_list_deinit();
}

TEST_F(IoDevTestSuite, DrainTimerCancel) {
  int rc;
  struct cras_rstream rstream;

  memset(&rstream, 0, sizeof(rstream));

  cras_iodev_list_init();

  d1_.direction = CRAS_STREAM_OUTPUT;
  rc = cras_iodev_list_add_output(&d1_);
  EXPECT_EQ(rc, 0);

  d1_.format = &fmt_;

  {
    CLEAR_AND_EVENTUALLY(EXPECT_EQ, audio_thread_add_open_dev_called, 0);
    CLEAR_AND_EVENTUALLY(EXPECT_EQ, audio_thread_rm_open_dev_called, 0);

    cras_iodev_list_add_active_node(CRAS_STREAM_OUTPUT,
                                    cras_make_node_id(d1_.info.idx, 1));
  }

  {  // If a stream is added, the device should be opened.
    CLEAR_AND_EVENTUALLY(EXPECT_EQ, audio_thread_add_open_dev_called, 1);

    DL_APPEND(stream_list_get_ret, &rstream);
    stream_add_cb(&rstream);
  }

  {
    CLEAR_AND_EVENTUALLY(EXPECT_EQ, audio_thread_drain_stream_called, 1);
    CLEAR_AND_EVENTUALLY(EXPECT_EQ, audio_thread_rm_open_dev_called, 0);

    audio_thread_drain_stream_return = 0;

    EVENTUALLY(EXPECT_EQ, rc, 0);

    DL_DELETE(stream_list_get_ret, &rstream);
    rc = stream_rm_cb(&rstream);
  }

  {  // Add stream again,
    CLEAR_AND_EVENTUALLY(EXPECT_EQ, audio_thread_add_open_dev_called, 0);
    CLEAR_AND_EVENTUALLY(EXPECT_EQ, audio_thread_rm_open_dev_called, 0);

    DL_APPEND(stream_list_get_ret, &rstream);
    stream_add_cb(&rstream);
  }

  {  // ... and make sure device isn't closed after timeout.
    CLEAR_AND_EVENTUALLY(EXPECT_EQ, audio_thread_rm_open_dev_called, 0);

    clock_gettime_retspec.tv_sec += 30;

    cras_tm_timer_cb(NULL, NULL);
  }

  {  // Remove stream,
    CLEAR_AND_EVENTUALLY(EXPECT_EQ, audio_thread_drain_stream_called, 1);
    CLEAR_AND_EVENTUALLY(EXPECT_EQ, audio_thread_rm_open_dev_called, 0);

    audio_thread_drain_stream_called = 0;

    EVENTUALLY(EXPECT_EQ, rc, 0);

    DL_DELETE(stream_list_get_ret, &rstream);
    rc = stream_rm_cb(&rstream);
  }

  {  // ... and check the device is eventually closed.
    CLEAR_AND_EVENTUALLY(EXPECT_EQ, audio_thread_rm_open_dev_called, 1);

    clock_gettime_retspec.tv_sec += 30;

    cras_tm_timer_cb(NULL, NULL);
  }

  cras_iodev_list_deinit();
}

TEST_F(IoDevTestSuite, RemoveThenSelectActiveNode) {
  int rc;
  cras_node_id_t id;

  cras_iodev_list_init();

  d1_.direction = CRAS_STREAM_OUTPUT;
  d2_.direction = CRAS_STREAM_OUTPUT;

  // d1_ will be the default_output
  rc = cras_iodev_list_add_output(&d1_);
  ASSERT_EQ(rc, 0);
  rc = cras_iodev_list_add_output(&d2_);
  ASSERT_EQ(rc, 0);

  /* Test the scenario that the selected active output removed
   * from active dev list, should be able to select back again. */
  id = cras_make_node_id(d2_.info.idx, 1);

  cras_iodev_list_rm_active_node(CRAS_STREAM_OUTPUT, id);
  ASSERT_EQ(0, audio_thread_rm_open_dev_called);

  cras_iodev_list_deinit();
}

TEST_F(IoDevTestSuite, SuspendDevWithPinnedStream) {
  int rc;
  struct cras_rstream rstream1, rstream2;

  cras_iodev_list_init();

  d1_.direction = CRAS_STREAM_OUTPUT;
  d1_.info.idx = 1;
  d2_.direction = CRAS_STREAM_OUTPUT;
  d2_.info.idx = 2;

  rc = cras_iodev_list_add_output(&d1_);
  EXPECT_EQ(rc, 0);
  rc = cras_iodev_list_add_output(&d2_);
  ASSERT_EQ(rc, 0);

  memset(&rstream1, 0, sizeof(rstream1));
  memset(&rstream2, 0, sizeof(rstream2));

  rstream2.is_pinned = 1;
  rstream2.pinned_dev_idx = d1_.info.idx;

  d1_.format = &fmt_;

  cras_iodev_list_select_node(CRAS_STREAM_OUTPUT,
                              cras_make_node_id(d2_.info.idx, 0));

  {  // Add a normal stream
    CLEAR_AND_EVENTUALLY(EXPECT_EQ, audio_thread_add_open_dev_called, 1);

    DL_APPEND(stream_list_get_ret, &rstream1);
    stream_add_cb(&rstream1);
  }

  {  // Add a pinned stream, expect another dev open call triggered.
    CLEAR_AND_EVENTUALLY(EXPECT_EQ, cras_iodev_open_called, 1);

    DL_APPEND(stream_list_get_ret, &rstream2);
    stream_add_cb(&rstream2);
  }

  {  // Suspend d1_ and make sure d1_ gets closed.
    CLEAR_AND_EVENTUALLY(EXPECT_EQ, audio_thread_rm_open_dev_called, 1);
    CLEAR_AND_EVENTUALLY(EXPECT_EQ, cras_iodev_close_called, 1);
    CLEAR_AND_EVENTUALLY(EXPECT_EQ, cras_iodev_close_dev, &d1_);
    CLEAR_AND_EVENTUALLY(EXPECT_EQ, update_active_node_called, 1);

    cras_iodev_list_suspend_dev(d1_.info.idx);
  }

  {
    CLEAR_AND_EVENTUALLY(EXPECT_EQ, audio_thread_add_open_dev_called, 1);
    CLEAR_AND_EVENTUALLY(EXPECT_EQ, cras_iodev_open_called, 1);
    CLEAR_AND_EVENTUALLY(EXPECT_EQ, update_active_node_called, 1);

    // Expect only the pinned stream got added.
    CLEAR_AND_EVENTUALLY(EXPECT_EQ, audio_thread_add_stream_called, 1);

    cras_iodev_list_resume_dev(d1_.info.idx);
  }

  cras_iodev_list_deinit();
}

TEST_F(IoDevTestSuite, DisableDevWithPinnedStream) {
  int rc;
  struct cras_rstream rstream1;

  memset(&rstream1, 0, sizeof(rstream1));

  cras_iodev_list_init();

  d1_.direction = CRAS_STREAM_OUTPUT;
  d1_.info.idx = 1;
  d2_.direction = CRAS_STREAM_OUTPUT;
  d2_.info.idx = 2;

  rc = cras_iodev_list_add_output(&d1_);
  EXPECT_EQ(rc, 0);
  rc = cras_iodev_list_add_output(&d2_);
  ASSERT_EQ(rc, 0);

  rstream1.is_pinned = 1;
  rstream1.pinned_dev_idx = d1_.info.idx;

  d1_.format = &fmt_;

  {
    CLEAR_AND_EVENTUALLY(EXPECT_EQ, audio_thread_add_open_dev_called, 0);
    CLEAR_AND_EVENTUALLY(EXPECT_EQ, audio_thread_rm_open_dev_called, 0);

    cras_iodev_list_select_node(CRAS_STREAM_OUTPUT,
                                cras_make_node_id(d1_.info.idx, 0));
  }

  {  // Add a pinned stream.
    CLEAR_AND_EVENTUALLY(EXPECT_EQ, audio_thread_add_open_dev_called, 1);
    CLEAR_AND_EVENTUALLY(EXPECT_EQ, cras_iodev_open_called, 1);

    DL_APPEND(stream_list_get_ret, &rstream1);
    stream_add_cb(&rstream1);
  }

  stream_list_has_pinned_stream_ret[d1_.info.idx] = 1;

  {  // Selects to d2_ expect no close dev triggered because pinned stream.
    CLEAR_AND_EVENTUALLY(EXPECT_EQ, audio_thread_rm_open_dev_called, 0);
    CLEAR_AND_EVENTUALLY(EXPECT_EQ, cras_iodev_close_called, 0);

    cras_iodev_list_select_node(CRAS_STREAM_OUTPUT,
                                cras_make_node_id(d2_.info.idx, 0));
  }

  cras_iodev_list_deinit();
}

TEST_F(IoDevTestSuite, AddRemovePinnedStream) {
  struct cras_rstream rstream;
  int rc;

  memset(&rstream, 0, sizeof(rstream));

  cras_iodev_list_init();

  d1_.direction = CRAS_STREAM_OUTPUT;
  d1_.info.idx = 1;
  EXPECT_EQ(cras_iodev_list_add_output(&d1_), 0);

  {  // Update d1_, then update fallback (disable)
    CLEAR_AND_EVENTUALLY(EXPECT_EQ, update_active_node_called, 2);

    EVENTUALLY(EXPECT_EQ, update_active_node_iodev_val[0], &d1_);
    EVENTUALLY(EXPECT_EQ, update_active_node_iodev_val[1],
               fallback_devs[CRAS_STREAM_OUTPUT]);

    cras_iodev_list_select_node(CRAS_STREAM_OUTPUT,
                                cras_make_node_id(d1_.info.idx, 0));
  }

  d2_.direction = CRAS_STREAM_OUTPUT;
  d2_.info.idx = 2;
  EXPECT_EQ(cras_iodev_list_add_output(&d2_), 0);

  d1_.format = &fmt_;
  d2_.format = &fmt_;

  // Setup pinned stream.
  rstream.is_pinned = 1;
  rstream.pinned_dev_idx = d1_.info.idx;

  {  // Add pinned stream to d1.
    CLEAR_AND_EVENTUALLY(EXPECT_EQ, audio_thread_add_stream_called, 1);
    CLEAR_AND_EVENTUALLY(EXPECT_EQ, audio_thread_add_stream_dev, &d1_);
    CLEAR_AND_EVENTUALLY(EXPECT_EQ, audio_thread_add_stream_stream, &rstream);

    // Init d1_ because of pinned stream
    CLEAR_AND_EVENTUALLY(EXPECT_EQ, update_active_node_called, 1);
    EVENTUALLY(EXPECT_EQ, update_active_node_iodev_val[0], &d1_);

    EVENTUALLY(EXPECT_EQ, rc, 0);

    DL_APPEND(stream_list_get_ret, &rstream);
    rc = stream_add_cb(&rstream);
  }

  stream_list_has_pinned_stream_ret[d1_.info.idx] = 1;

  {  // Select d2, check pinned stream is not added to d2.
    CLEAR_AND_EVENTUALLY(EXPECT_EQ, audio_thread_add_stream_called, 0);
    CLEAR_AND_EVENTUALLY(EXPECT_EQ, update_active_node_called, 2);

    // Unselect d1_ (disable but will not call update_active_node due to pinned)
    // and select d2_
    EVENTUALLY(EXPECT_EQ, update_active_node_iodev_val[0], &d2_);
    EVENTUALLY(EXPECT_EQ, update_active_node_iodev_val[1],
               fallback_devs[CRAS_STREAM_OUTPUT]);

    cras_iodev_list_select_node(CRAS_STREAM_OUTPUT,
                                cras_make_node_id(d2_.info.idx, 0));
  }

  stream_list_has_pinned_stream_ret[d1_.info.idx] = 0;

  {  // Remove pinned stream from d1, check d1 is closed after stream removed.
    CLEAR_AND_EVENTUALLY(EXPECT_EQ, cras_iodev_close_called, 1);
    CLEAR_AND_EVENTUALLY(EXPECT_EQ, cras_iodev_close_dev, &d1_);

    // close pinned device
    CLEAR_AND_EVENTUALLY(EXPECT_EQ, update_active_node_called, 1);
    EVENTUALLY(EXPECT_EQ, update_active_node_iodev_val[0], &d1_);

    EVENTUALLY(EXPECT_EQ, rc, 0);

    DL_DELETE(stream_list_get_ret, &rstream);
    rc = stream_rm_cb(&rstream);
  }

  {  // Assume dev is already opened, add pin stream should not trigger another
     // update_active_node call, but will trigger audio_thread_add_stream.
    CLEAR_AND_EVENTUALLY(EXPECT_EQ, update_active_node_called, 0);
    CLEAR_AND_EVENTUALLY(EXPECT_EQ, audio_thread_add_stream_called, 1);

    audio_thread_is_dev_open_ret = 1;

    EVENTUALLY(EXPECT_EQ, rc, 0);

    DL_APPEND(stream_list_get_ret, &rstream);
    rc = stream_add_cb(&rstream);
  }

  cras_iodev_list_deinit();
}

TEST_F(IoDevTestSuite, SuspendResumePinnedStream) {
  struct cras_rstream rstream;
  int rc;

  memset(&rstream, 0, sizeof(rstream));

  cras_iodev_list_init();

  // Add 2 output devices.
  d1_.direction = CRAS_STREAM_OUTPUT;
  EXPECT_EQ(cras_iodev_list_add_output(&d1_), 0);
  d2_.direction = CRAS_STREAM_OUTPUT;
  EXPECT_EQ(cras_iodev_list_add_output(&d2_), 0);

  d1_.format = &fmt_;
  d2_.format = &fmt_;

  // Setup pinned stream.
  rstream.is_pinned = 1;
  rstream.pinned_dev_idx = d1_.info.idx;

  {  // Add pinned stream to d1.
    CLEAR_AND_EVENTUALLY(EXPECT_EQ, audio_thread_add_stream_called, 1);
    CLEAR_AND_EVENTUALLY(EXPECT_EQ, audio_thread_add_stream_dev, &d1_);
    CLEAR_AND_EVENTUALLY(EXPECT_EQ, audio_thread_add_stream_stream, &rstream);

    EVENTUALLY(EXPECT_EQ, rc, 0);

    DL_APPEND(stream_list_get_ret, &rstream);
    rc = stream_add_cb(&rstream);
  }

  // Test for suspend

  // Device state enters no_stream after stream is disconnected.
  d1_.state = CRAS_IODEV_STATE_NO_STREAM_RUN;

  // Device has no pinned stream now. But this pinned stream remains in
  // stream_list.
  stream_list_has_pinned_stream_ret[d1_.info.idx] = 0;

  {  // Suspend and verify that stream is disconnected and d1 is closed.
    CLEAR_AND_EVENTUALLY(EXPECT_EQ, audio_thread_disconnect_stream_called, 1);
    CLEAR_AND_EVENTUALLY(EXPECT_EQ, audio_thread_disconnect_stream_stream,
                         &rstream);
    CLEAR_AND_EVENTUALLY(EXPECT_EQ, cras_iodev_close_called, 1);
    CLEAR_AND_EVENTUALLY(EXPECT_EQ, cras_iodev_close_dev, &d1_);

    observer_ops->suspend_changed(NULL, 1);
  }

  // Test for resume
  d1_.state = CRAS_IODEV_STATE_CLOSE;

  {  // Verify that device is opened and stream is attached to the device.
    CLEAR_AND_EVENTUALLY(EXPECT_EQ, cras_iodev_open_called, 1);
    CLEAR_AND_EVENTUALLY(EXPECT_EQ, audio_thread_add_stream_called, 1);
    CLEAR_AND_EVENTUALLY(EXPECT_EQ, audio_thread_add_stream_stream, &rstream);

    observer_ops->suspend_changed(NULL, 0);
  }

  cras_iodev_list_deinit();
}

TEST_F(IoDevTestSuite, HotwordStreamsAddedThenSuspendResume) {
  struct cras_rstream rstream;
  int rc;

  memset(&rstream, 0, sizeof(rstream));

  cras_iodev_list_init();

  node1.type = CRAS_NODE_TYPE_HOTWORD;
  d1_.direction = CRAS_STREAM_INPUT;
  EXPECT_EQ(cras_iodev_list_add_input(&d1_), 0);

  d1_.format = &fmt_;

  rstream.is_pinned = 1;
  rstream.pinned_dev_idx = d1_.info.idx;
  rstream.flags = HOTWORD_STREAM;

  {  // Add a hotword stream.
    CLEAR_AND_EVENTUALLY(EXPECT_EQ, audio_thread_add_stream_called, 1);
    CLEAR_AND_EVENTUALLY(EXPECT_EQ, audio_thread_add_stream_dev, &d1_);
    CLEAR_AND_EVENTUALLY(EXPECT_EQ, audio_thread_add_stream_stream, &rstream);

    EVENTUALLY(EXPECT_EQ, rc, 0);

    DL_APPEND(stream_list_get_ret, &rstream);
    rc = stream_add_cb(&rstream);
  }

  { /* Suspend hotword streams, verify the existing stream disconnects
     * from the hotword device and connects to the empty iodev. */
    CLEAR_AND_EVENTUALLY(EXPECT_EQ, audio_thread_disconnect_stream_called, 1);
    CLEAR_AND_EVENTUALLY(EXPECT_EQ, audio_thread_disconnect_stream_stream,
                         &rstream);
    CLEAR_AND_EVENTUALLY(EXPECT_EQ, audio_thread_disconnect_stream_dev, &d1_);
    CLEAR_AND_EVENTUALLY(EXPECT_EQ, audio_thread_add_stream_called, 1);
    CLEAR_AND_EVENTUALLY(EXPECT_EQ, audio_thread_add_stream_stream, &rstream);
    CLEAR_AND_EVENTUALLY(EXPECT_EQ, audio_thread_add_stream_dev,
                         empty_hotword_dev);

    EVENTUALLY(EXPECT_EQ, rc, 0);

    rc = cras_iodev_list_suspend_hotword_streams();
  }

  { /* Resume hotword streams, verify the stream disconnects from
     * the empty iodev and connects back to the real hotword iodev. */
    CLEAR_AND_EVENTUALLY(EXPECT_EQ, audio_thread_disconnect_stream_called, 1);
    CLEAR_AND_EVENTUALLY(EXPECT_EQ, audio_thread_disconnect_stream_stream,
                         &rstream);
    CLEAR_AND_EVENTUALLY(EXPECT_EQ, audio_thread_disconnect_stream_dev,
                         empty_hotword_dev);
    CLEAR_AND_EVENTUALLY(EXPECT_EQ, audio_thread_add_stream_called, 1);
    CLEAR_AND_EVENTUALLY(EXPECT_EQ, audio_thread_add_stream_stream, &rstream);
    CLEAR_AND_EVENTUALLY(EXPECT_EQ, audio_thread_add_stream_dev, &d1_);

    EVENTUALLY(EXPECT_EQ, rc, 0);

    rc = cras_iodev_list_resume_hotword_stream();
  }

  cras_iodev_list_deinit();
}

TEST_F(IoDevTestSuite, HotwordStreamsAddedAfterSuspend) {
  struct cras_rstream rstream;
  int rc;

  memset(&rstream, 0, sizeof(rstream));

  cras_iodev_list_init();

  node1.type = CRAS_NODE_TYPE_HOTWORD;
  d1_.direction = CRAS_STREAM_INPUT;
  EXPECT_EQ(cras_iodev_list_add_input(&d1_), 0);

  d1_.format = &fmt_;

  rstream.is_pinned = 1;
  rstream.pinned_dev_idx = d1_.info.idx;
  rstream.flags = HOTWORD_STREAM;

  {  // Suspends hotword streams before a stream connected.
    CLEAR_AND_EVENTUALLY(EXPECT_EQ, audio_thread_disconnect_stream_called, 0);
    CLEAR_AND_EVENTUALLY(EXPECT_EQ, audio_thread_add_stream_called, 0);

    EVENTUALLY(EXPECT_EQ, rc, 0);

    rc = cras_iodev_list_suspend_hotword_streams();
  }

  {  // Hotword stream connected, verify it is added to the empty iodev.
    CLEAR_AND_EVENTUALLY(EXPECT_EQ, audio_thread_add_stream_called, 1);
    CLEAR_AND_EVENTUALLY(EXPECT_EQ, audio_thread_add_stream_dev,
                         empty_hotword_dev);
    CLEAR_AND_EVENTUALLY(EXPECT_EQ, audio_thread_add_stream_stream, &rstream);

    EVENTUALLY(EXPECT_EQ, rc, 0);

    DL_APPEND(stream_list_get_ret, &rstream);
    rc = stream_add_cb(&rstream);
  }

  { /* Resume hotword streams, now the existing hotword stream should disconnect
     * from the empty iodev and connect to the real hotword iodev. */
    CLEAR_AND_EVENTUALLY(EXPECT_EQ, audio_thread_disconnect_stream_called, 1);
    CLEAR_AND_EVENTUALLY(EXPECT_EQ, audio_thread_disconnect_stream_stream,
                         &rstream);
    CLEAR_AND_EVENTUALLY(EXPECT_EQ, audio_thread_disconnect_stream_dev,
                         empty_hotword_dev);
    CLEAR_AND_EVENTUALLY(EXPECT_EQ, audio_thread_add_stream_called, 1);
    CLEAR_AND_EVENTUALLY(EXPECT_EQ, audio_thread_add_stream_stream, &rstream);
    CLEAR_AND_EVENTUALLY(EXPECT_EQ, audio_thread_add_stream_dev, &d1_);

    EVENTUALLY(EXPECT_EQ, rc, 0);

    rc = cras_iodev_list_resume_hotword_stream();
  }

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
  int rc;

  memset(&rstream, 0, sizeof(rstream));

  cras_iodev_list_init();

  node1.type = CRAS_NODE_TYPE_HOTWORD;
  d1_.direction = CRAS_STREAM_INPUT;
  EXPECT_EQ(cras_iodev_list_add_input(&d1_), 0);

  d1_.format = &fmt_;

  rstream.is_pinned = 1;
  rstream.pinned_dev_idx = d1_.info.idx;
  rstream.flags = HOTWORD_STREAM;

  {  // Add a hotword stream.
    CLEAR_AND_EVENTUALLY(EXPECT_EQ, audio_thread_add_stream_called, 1);
    CLEAR_AND_EVENTUALLY(EXPECT_EQ, audio_thread_add_stream_dev, &d1_);
    CLEAR_AND_EVENTUALLY(EXPECT_EQ, audio_thread_add_stream_stream, &rstream);

    EVENTUALLY(EXPECT_EQ, rc, 0);

    DL_APPEND(stream_list_get_ret, &rstream);
    rc = stream_add_cb(&rstream);
  }

  server_state_hotword_pause_at_suspend = 1;

  {  // Trigger system suspend. Verify hotword stream is moved to empty dev.
    CLEAR_AND_EVENTUALLY(EXPECT_EQ, audio_thread_disconnect_stream_called, 1);
    CLEAR_AND_EVENTUALLY(EXPECT_EQ, audio_thread_disconnect_stream_stream,
                         &rstream);
    CLEAR_AND_EVENTUALLY(EXPECT_EQ, audio_thread_disconnect_stream_dev, &d1_);
    CLEAR_AND_EVENTUALLY(EXPECT_EQ, audio_thread_add_stream_called, 1);
    CLEAR_AND_EVENTUALLY(EXPECT_EQ, audio_thread_add_stream_stream, &rstream);
    CLEAR_AND_EVENTUALLY(EXPECT_EQ, audio_thread_add_stream_dev,
                         empty_hotword_dev);

    observer_ops->suspend_changed(NULL, 1);
  }

  {  // Trigger system resume. Verify hotword stream is moved to real dev.
    CLEAR_AND_EVENTUALLY(EXPECT_EQ, audio_thread_disconnect_stream_called, 1);
    CLEAR_AND_EVENTUALLY(EXPECT_EQ, audio_thread_disconnect_stream_stream,
                         &rstream);
    CLEAR_AND_EVENTUALLY(EXPECT_EQ, audio_thread_disconnect_stream_dev,
                         empty_hotword_dev);
    CLEAR_AND_EVENTUALLY(EXPECT_EQ, audio_thread_add_stream_called, 1);
    CLEAR_AND_EVENTUALLY(EXPECT_EQ, audio_thread_add_stream_stream, &rstream);
    CLEAR_AND_EVENTUALLY(EXPECT_EQ, audio_thread_add_stream_dev, &d1_);

    observer_ops->suspend_changed(NULL, 0);
  }

  server_state_hotword_pause_at_suspend = 0;

  {  // Trigger system suspend. Verify hotword stream is not touched.
    CLEAR_AND_EVENTUALLY(EXPECT_EQ, audio_thread_disconnect_stream_called, 0);
    CLEAR_AND_EVENTUALLY(EXPECT_EQ, audio_thread_add_stream_called, 0);

    observer_ops->suspend_changed(NULL, 1);
  }

  {  // Trigger system resume. Verify hotword stream is not touched.
    CLEAR_AND_EVENTUALLY(EXPECT_EQ, audio_thread_disconnect_stream_called, 0);
    CLEAR_AND_EVENTUALLY(EXPECT_EQ, audio_thread_add_stream_called, 0);

    observer_ops->suspend_changed(NULL, 0);
  }

  cras_iodev_list_deinit();
}

TEST_F(IoDevTestSuite, SetNoiseCancellation) {
  struct cras_rstream rstream;
  int rc;

  memset(&rstream, 0, sizeof(rstream));

  cras_iodev_list_init();

  d1_.info.idx = 1;
  d1_.direction = CRAS_STREAM_INPUT;
  rc = cras_iodev_list_add_input(&d1_);
  ASSERT_EQ(rc, 0);

  d1_.format = &fmt_;

  rstream.direction = CRAS_STREAM_INPUT;

  cras_iodev_list_select_node(CRAS_STREAM_INPUT,
                              cras_make_node_id(d1_.info.idx, 0));

  {
    CLEAR_AND_EVENTUALLY(EXPECT_EQ, audio_thread_add_stream_called, 1);
    CLEAR_AND_EVENTUALLY(EXPECT_EQ, audio_thread_add_open_dev_called, 1);

    DL_APPEND(stream_list_get_ret, &rstream);
    stream_add_cb(&rstream);
  }

  {  // reset_for_noise_cancellation causes device suspend & resume
     // While suspending d1_: rm d1_, open fallback
     // While resuming d1_: rm fallback, open d1_
    CLEAR_AND_EVENTUALLY(EXPECT_EQ, cras_iodev_open_fallback_called, 1);
    CLEAR_AND_EVENTUALLY(EXPECT_EQ, audio_thread_add_open_dev_fallback_called,
                         1);

    CLEAR_AND_EVENTUALLY(EXPECT_EQ, cras_iodev_close_called, 1);
    CLEAR_AND_EVENTUALLY(EXPECT_EQ, audio_thread_rm_open_dev_called, 1);

    CLEAR_AND_EVENTUALLY(EXPECT_EQ, cras_iodev_open_called, 1);
    CLEAR_AND_EVENTUALLY(EXPECT_EQ, audio_thread_add_open_dev_called, 1);

    CLEAR_AND_EVENTUALLY(EXPECT_EQ, cras_iodev_close_fallback_called, 1);
    CLEAR_AND_EVENTUALLY(EXPECT_EQ, audio_thread_rm_open_dev_fallback_called,
                         1);

    cras_iodev_list_reset_for_noise_cancellation();
  }

  cras_iodev_list_deinit();
}

TEST_F(IoDevTestSuite, BlockNoiseCancellationByActiveSpeaker) {
  cras_iodev_list_init();

  d1_.info.idx = 1;
  d1_.direction = CRAS_STREAM_INPUT;
  node1.nc_provider = CRAS_IONODE_NC_PROVIDER_DSP;
  d2_.info.idx = 2;
  d2_.direction = CRAS_STREAM_OUTPUT;
  node2.type = CRAS_NODE_TYPE_USB;
  d3_.info.idx = 3;
  d3_.direction = CRAS_STREAM_OUTPUT;
  node3.type = CRAS_NODE_TYPE_INTERNAL_SPEAKER;

  // Check no devices exist initially.
  EXPECT_EQ(cras_iodev_list_get_inputs(NULL), 0);

  EXPECT_EQ(cras_iodev_list_add_input(&d1_), 0);
  EXPECT_GE(d1_.info.idx, 0);
  EXPECT_EQ(cras_iodev_list_add_output(&d2_), 0);
  EXPECT_GE(d2_.info.idx, 1);
  EXPECT_EQ(cras_iodev_list_add_output(&d3_), 0);
  EXPECT_GE(d3_.info.idx, 2);

  // Make sure shared state was updated.
  EXPECT_EQ(server_state_stub.num_input_devs, 1);
  ASSERT_EQ(server_state_stub.num_input_nodes, 1);
  EXPECT_EQ(server_state_stub.input_nodes[0].audio_effect,
            EFFECT_TYPE_NOISE_CANCELLATION);
  EXPECT_EQ(cras_observer_notify_nodes_called, 0);

  // Block Noise Cancellation in audio_effect.
  cras_iodev_list_select_node(CRAS_STREAM_OUTPUT,
                              cras_make_node_id(d2_.info.idx, 0));
  ASSERT_EQ(server_state_stub.num_input_nodes, 1);
  EXPECT_EQ(server_state_stub.input_nodes[0].audio_effect, 0);
  EXPECT_EQ(cras_observer_notify_nodes_called, 1);

  // Unblock Noise Cancellation in audio_effect.
  cras_iodev_list_select_node(CRAS_STREAM_OUTPUT,
                              cras_make_node_id(d3_.info.idx, 0));
  ASSERT_EQ(server_state_stub.num_input_nodes, 1);
  EXPECT_EQ(server_state_stub.input_nodes[0].audio_effect,
            EFFECT_TYPE_NOISE_CANCELLATION);
  EXPECT_EQ(cras_observer_notify_nodes_called, 2);

  cras_iodev_list_deinit();
}

TEST_F(IoDevTestSuite, BlockNoiseCancellationByPinnedSpeaker) {
  struct cras_rstream rstream1, rstream2;
  int rc;

  memset(&rstream1, 0, sizeof(rstream1));
  memset(&rstream2, 0, sizeof(rstream2));

  cras_iodev_list_init();

  // Add 2 output devices.
  d1_.direction = CRAS_STREAM_OUTPUT;
  d1_.info.idx = 1;
  node1.idx = 1;
  node1.type = CRAS_NODE_TYPE_USB;
  EXPECT_EQ(cras_iodev_list_add_output(&d1_), 0);

  d2_.direction = CRAS_STREAM_OUTPUT;
  d2_.info.idx = 2;
  node2.idx = 2;
  node2.type = CRAS_NODE_TYPE_INTERNAL_SPEAKER;
  EXPECT_EQ(cras_iodev_list_add_output(&d2_), 0);

  // Add 1 input device for checking Noise Cancellation state.
  d3_.direction = CRAS_STREAM_INPUT;
  node3.nc_provider = CRAS_IONODE_NC_PROVIDER_DSP;
  EXPECT_EQ(cras_iodev_list_add_input(&d3_), 0);

  // Make sure shared state was updated.
  EXPECT_EQ(server_state_stub.num_input_devs, 1);
  ASSERT_EQ(server_state_stub.num_input_nodes, 1);
  EXPECT_EQ(server_state_stub.input_nodes[0].audio_effect,
            EFFECT_TYPE_NOISE_CANCELLATION);
  EXPECT_EQ(cras_observer_notify_nodes_called, 0);

  // Select usb playback as the active node.
  cras_iodev_list_select_node(CRAS_STREAM_OUTPUT,
                              cras_make_node_id(d1_.info.idx, node1.idx));

  // Block Noise Cancellation because usb player is enabled.
  ASSERT_EQ(server_state_stub.num_input_nodes, 1);
  EXPECT_EQ(server_state_stub.input_nodes[0].audio_effect, 0);
  EXPECT_EQ(cras_observer_notify_nodes_called, 1);

  // Select internal speaker as the active node.
  cras_iodev_list_select_node(CRAS_STREAM_OUTPUT,
                              cras_make_node_id(d2_.info.idx, node2.idx));

  // Unblock Noise Cancellation because usb player is disabled.
  ASSERT_EQ(server_state_stub.num_input_nodes, 1);
  EXPECT_EQ(server_state_stub.input_nodes[0].audio_effect,
            EFFECT_TYPE_NOISE_CANCELLATION);
  EXPECT_EQ(cras_observer_notify_nodes_called, 2);

  d1_.format = &fmt_;
  d2_.format = &fmt_;

  // Setup pinned streams.
  rstream1.is_pinned = 1;
  rstream1.direction = CRAS_STREAM_OUTPUT;
  rstream1.pinned_dev_idx = d1_.info.idx;
  rstream2.is_pinned = 1;
  rstream2.direction = CRAS_STREAM_OUTPUT;
  rstream2.pinned_dev_idx = d2_.info.idx;

  {  // Add pinned stream to d1 (usb).
    CLEAR_AND_EVENTUALLY(EXPECT_EQ, audio_thread_add_stream_called, 1);
    CLEAR_AND_EVENTUALLY(EXPECT_EQ, audio_thread_add_stream_dev, &d1_);
    CLEAR_AND_EVENTUALLY(EXPECT_EQ, audio_thread_add_stream_stream, &rstream1);
    CLEAR_AND_EVENTUALLY(EXPECT_EQ, update_active_node_called, 1);
    EVENTUALLY(EXPECT_EQ, update_active_node_iodev_val[0], &d1_);
    CLEAR_AND_EVENTUALLY(EXPECT_EQ, cras_observer_notify_nodes_called, 1);

    // Noise Cancellation is blocked by pinned stream via usb player.
    EVENTUALLY(EXPECT_EQ, server_state_stub.num_input_nodes, 1);
    EVENTUALLY(EXPECT_EQ, server_state_stub.input_nodes[0].audio_effect, 0);

    EVENTUALLY(EXPECT_EQ, rc, 0);

    DL_APPEND(stream_list_get_ret, &rstream1);
    rc = stream_add_cb(&rstream1);
  }

  {  // Add pinned stream to d2 (internal speaker).
    CLEAR_AND_EVENTUALLY(EXPECT_EQ, audio_thread_add_stream_called, 1);
    CLEAR_AND_EVENTUALLY(EXPECT_EQ, audio_thread_add_stream_dev, &d2_);
    CLEAR_AND_EVENTUALLY(EXPECT_EQ, audio_thread_add_stream_stream, &rstream2);
    CLEAR_AND_EVENTUALLY(EXPECT_EQ, update_active_node_called, 1);
    EVENTUALLY(EXPECT_EQ, update_active_node_iodev_val[0], &d2_);
    CLEAR_AND_EVENTUALLY(EXPECT_EQ, cras_observer_notify_nodes_called, 0);

    // Nothing changed for adding pinned stream to d2.
    EVENTUALLY(ASSERT_EQ, server_state_stub.num_input_nodes, 1);
    EVENTUALLY(EXPECT_EQ, server_state_stub.input_nodes[0].audio_effect, 0);

    EVENTUALLY(EXPECT_EQ, rc, 0);

    DL_APPEND(stream_list_get_ret, &rstream2);
    rc = stream_add_cb(&rstream2);
  }

  stream_list_has_pinned_stream_ret[d1_.info.idx] = 1;
  stream_list_has_pinned_stream_ret[d2_.info.idx] = 0;

  {  // Remove pinned stream from d2.
    CLEAR_AND_EVENTUALLY(EXPECT_EQ, cras_observer_notify_nodes_called, 0);

    // Nothing changed for removing pinned stream from d2.
    EVENTUALLY(ASSERT_EQ, server_state_stub.num_input_nodes, 1);
    EVENTUALLY(EXPECT_EQ, server_state_stub.input_nodes[0].audio_effect, 0);

    EVENTUALLY(EXPECT_EQ, rc, 0);

    DL_DELETE(stream_list_get_ret, &rstream2);
    rc = stream_rm_cb(&rstream2);
  }

  stream_list_has_pinned_stream_ret[d1_.info.idx] = 0;

  {  // Remove pinned stream from d1.
    CLEAR_AND_EVENTUALLY(EXPECT_EQ, cras_observer_notify_nodes_called, 1);

    // Unblock Noise Cancellation because pinned stream is removed from d1.
    EVENTUALLY(ASSERT_EQ, server_state_stub.num_input_nodes, 1);
    EVENTUALLY(EXPECT_EQ, server_state_stub.input_nodes[0].audio_effect,
               EFFECT_TYPE_NOISE_CANCELLATION);

    EVENTUALLY(EXPECT_EQ, rc, 0);

    DL_DELETE(stream_list_get_ret, &rstream1);
    rc = stream_rm_cb(&rstream1);
  }

  cras_iodev_list_deinit();
}

TEST_F(IoDevTestSuite, SetAecRefReconnectStream) {
  struct cras_rstream rstream;
  int rc;

  memset(&rstream, 0, sizeof(rstream));

  cras_iodev_list_init();

  d1_.direction = CRAS_STREAM_OUTPUT;
  rc = cras_iodev_list_add_output(&d1_);
  ASSERT_EQ(rc, 0);

  // Prepare an enabled input iodev for stream to attach to.
  d2_.direction = CRAS_STREAM_INPUT;
  d2_.info.idx = 2;
  EXPECT_EQ(cras_iodev_list_add_input(&d2_), 0);

  cras_iodev_list_select_node(CRAS_STREAM_INPUT,
                              cras_make_node_id(d2_.info.idx, 0));

  rstream.direction = CRAS_STREAM_INPUT;
  rstream.stream_id = 123;
  rstream.stream_apm = reinterpret_cast<struct cras_stream_apm*>(0x987);

  DL_APPEND(stream_list_get_ret, &rstream);
  stream_add_cb(&rstream);

  { /* Verify the stream and apm went through correct life cycles. Because
     * setting AEC ref is expected to trigger stream reconnection. */
    CLEAR_AND_EVENTUALLY(EXPECT_EQ, cras_stream_apm_set_aec_ref_called, 1);
    CLEAR_AND_EVENTUALLY(EXPECT_EQ, audio_thread_disconnect_stream_called, 1);
    CLEAR_AND_EVENTUALLY(EXPECT_EQ, cras_stream_apm_remove_called, 1);
    CLEAR_AND_EVENTUALLY(EXPECT_EQ, cras_stream_apm_add_called, 1);
    CLEAR_AND_EVENTUALLY(EXPECT_EQ, audio_thread_add_stream_called, 1);

    cras_iodev_list_set_aec_ref(123, d1_.info.idx);
  }

  cras_iodev_list_deinit();
}

TEST_F(IoDevTestSuite, ReconnectStreamsWithApm) {
  struct cras_rstream rstream;
  int rc;

  memset(&rstream, 0, sizeof(rstream));
  cras_iodev_list_init();

  d1_.direction = CRAS_STREAM_INPUT;
  rc = cras_iodev_list_add_input(&d1_);
  ASSERT_EQ(rc, 0);

  // Prepare an enabled input iodev for stream to attach to.
  d2_.direction = CRAS_STREAM_INPUT;
  d2_.info.idx = 2;
  EXPECT_EQ(cras_iodev_list_add_input(&d2_), 0);
  cras_iodev_list_select_node(CRAS_STREAM_INPUT,
                              cras_make_node_id(d2_.info.idx, 0));

  rstream.direction = CRAS_STREAM_INPUT;
  rstream.stream_apm = reinterpret_cast<struct cras_stream_apm*>(0x987);

  DL_APPEND(stream_list_get_ret, &rstream);
  stream_add_cb(&rstream);

  {  // Verify the stream and apm to through correct life cycles.
    CLEAR_AND_EVENTUALLY(EXPECT_EQ, audio_thread_disconnect_stream_called, 1);
    CLEAR_AND_EVENTUALLY(EXPECT_EQ, cras_stream_apm_remove_called, 1);
    CLEAR_AND_EVENTUALLY(EXPECT_EQ, cras_stream_apm_add_called, 1);
    CLEAR_AND_EVENTUALLY(EXPECT_EQ, audio_thread_add_stream_called, 1);

    cras_iodev_list_reconnect_streams_with_apm();
  }

  cras_iodev_list_deinit();
}

TEST_F(IoDevTestSuite, BlockNoiseCancellationInHybridCases) {
  struct cras_rstream rstream;
  int rc;

  memset(&rstream, 0, sizeof(rstream));

  cras_iodev_list_init();

  // Add output device.
  d1_.direction = CRAS_STREAM_OUTPUT;
  d1_.info.idx = 1;
  node1.type = CRAS_NODE_TYPE_USB;
  EXPECT_EQ(cras_iodev_list_add_output(&d1_), 0);

  // Add input device for checking Noise Cancellation state.
  d2_.direction = CRAS_STREAM_INPUT;
  d2_.info.idx = 2;
  node2.nc_provider = CRAS_IONODE_NC_PROVIDER_DSP;
  EXPECT_EQ(cras_iodev_list_add_input(&d2_), 0);

  // Make sure shared state was updated.
  ASSERT_EQ(server_state_stub.num_input_nodes, 1);
  EXPECT_EQ(server_state_stub.input_nodes[0].audio_effect,
            EFFECT_TYPE_NOISE_CANCELLATION);
  EXPECT_EQ(cras_observer_notify_nodes_called, 0);

  {  // Select usb as the active node, which means usb device is enabled,
     // yet opened.
    CLEAR_AND_EVENTUALLY(EXPECT_EQ, cras_observer_notify_nodes_called, 1);
    EVENTUALLY(EXPECT_EQ, cras_iodev_list_dev_is_enabled(&d1_), 1);

    // Noise Cancellation is blocked.
    EVENTUALLY(ASSERT_EQ, server_state_stub.num_input_nodes, 1);
    EVENTUALLY(EXPECT_EQ, server_state_stub.input_nodes[0].audio_effect, 0);

    cras_iodev_list_select_node(CRAS_STREAM_OUTPUT,
                                cras_make_node_id(d1_.info.idx, 0));
  }

  d1_.format = &fmt_;

  // Setup pinned streams.
  rstream.is_pinned = 1;
  rstream.direction = CRAS_STREAM_OUTPUT;
  rstream.pinned_dev_idx = d1_.info.idx;

  {  // Add pinned stream to d1, which means usb device is opened.
    EVENTUALLY(EXPECT_EQ, cras_iodev_is_open(&d1_), 1);
    CLEAR_AND_EVENTUALLY(EXPECT_EQ, audio_thread_add_stream_called, 1);
    CLEAR_AND_EVENTUALLY(EXPECT_EQ, audio_thread_add_stream_dev, &d1_);
    CLEAR_AND_EVENTUALLY(EXPECT_EQ, audio_thread_add_stream_stream, &rstream);
    CLEAR_AND_EVENTUALLY(EXPECT_EQ, update_active_node_called, 1);
    EVENTUALLY(EXPECT_EQ, update_active_node_iodev_val[0], &d1_);
    CLEAR_AND_EVENTUALLY(EXPECT_EQ, cras_observer_notify_nodes_called, 0);

    // Noise Cancellation is still blocked. notify_nodes shouldn't be called.
    EVENTUALLY(ASSERT_EQ, server_state_stub.num_input_nodes, 1);
    EVENTUALLY(EXPECT_EQ, server_state_stub.input_nodes[0].audio_effect, 0);

    EVENTUALLY(EXPECT_EQ, rc, 0);

    DL_APPEND(stream_list_get_ret, &rstream);
    rc = stream_add_cb(&rstream);
  }

  stream_list_has_pinned_stream_ret[d1_.info.idx] = 0;

  {  // Remove pinned stream from d1, which means usb device is closed.
    CLEAR_AND_EVENTUALLY(EXPECT_EQ, cras_observer_notify_nodes_called, 0);
    EVENTUALLY(EXPECT_EQ, cras_iodev_list_dev_is_enabled(&d1_), 1);

    // Noise Cancellation is still blocked because usb device is still enabled.
    // notify_nodes shouldn't be called.
    EVENTUALLY(ASSERT_EQ, server_state_stub.num_input_nodes, 1);
    EVENTUALLY(EXPECT_EQ, server_state_stub.input_nodes[0].audio_effect, 0);

    EVENTUALLY(EXPECT_EQ, rc, 0);

    DL_DELETE(stream_list_get_ret, &rstream);
    rc = stream_rm_cb(&rstream);
  }

  {  // Disable usb device d1.
    CLEAR_AND_EVENTUALLY(EXPECT_EQ, cras_observer_notify_nodes_called, 1);
    EVENTUALLY(EXPECT_EQ, cras_iodev_list_dev_is_enabled(&d1_), 0);

    // Noise Cancellation is unblocked because usb device is
    // disabled (and closed).
    EVENTUALLY(ASSERT_EQ, server_state_stub.num_input_nodes, 1);
    EVENTUALLY(EXPECT_EQ, server_state_stub.input_nodes[0].audio_effect,
               EFFECT_TYPE_NOISE_CANCELLATION);

    cras_iodev_list_disable_dev(&d1_, false);
  }

  cras_iodev_list_deinit();
}

TEST_F(IoDevTestSuite, BlockNoiseCancellationByTwoNodesInOneDev) {
  struct cras_rstream rstream;
  int rc;

  struct cras_ionode node1_2;

  memset(&rstream, 0, sizeof(rstream));
  memset(&node1_2, 0, sizeof(node1_2));

  cras_iodev_list_init();

  // Add output device with two nodes (node1 is active node).
  d1_.direction = CRAS_STREAM_OUTPUT;
  d1_.info.idx = 1;
  d1_.update_active_node = set_active_node_by_id;
  d1_.nodes = NULL;
  node1.idx = 1;
  node1.type = CRAS_NODE_TYPE_HEADPHONE;
  DL_APPEND(d1_.nodes, &node1);
  node1_2.idx = 2;
  node1_2.type = CRAS_NODE_TYPE_INTERNAL_SPEAKER;
  DL_APPEND(d1_.nodes, &node1_2);
  EXPECT_EQ(cras_iodev_list_add_output(&d1_), 0);

  // Add input device for checking Noise Cancellation state.
  d2_.direction = CRAS_STREAM_INPUT;
  d2_.info.idx = 2;
  node2.idx = 2;
  node2.nc_provider = CRAS_IONODE_NC_PROVIDER_DSP;
  EXPECT_EQ(cras_iodev_list_add_input(&d2_), 0);

  // Make sure shared state was updated.
  ASSERT_EQ(server_state_stub.num_input_nodes, 1);
  EXPECT_EQ(server_state_stub.input_nodes[0].audio_effect,
            EFFECT_TYPE_NOISE_CANCELLATION);
  EXPECT_EQ(cras_observer_notify_nodes_called, 0);

  {  // Select headphone as the active node.
    EVENTUALLY(EXPECT_EQ, cras_iodev_list_dev_is_enabled(&d1_), 1);
    CLEAR_AND_EVENTUALLY(EXPECT_EQ, cras_observer_notify_nodes_called, 1);

    // Noise Cancellation is blocked.
    EVENTUALLY(ASSERT_EQ, server_state_stub.num_input_nodes, 1);
    EVENTUALLY(EXPECT_EQ, server_state_stub.input_nodes[0].audio_effect, 0);

    cras_iodev_list_select_node(CRAS_STREAM_OUTPUT,
                                cras_make_node_id(d1_.info.idx, node1.idx));
  }

  {  // Select internal speaker as the active node.
    EVENTUALLY(EXPECT_EQ, cras_iodev_list_dev_is_enabled(&d1_), 1);
    CLEAR_AND_EVENTUALLY(EXPECT_EQ, cras_observer_notify_nodes_called, 1);

    // Noise Cancellation is unblocked.
    EVENTUALLY(ASSERT_EQ, server_state_stub.num_input_nodes, 1);
    EVENTUALLY(EXPECT_EQ, server_state_stub.input_nodes[0].audio_effect,
               EFFECT_TYPE_NOISE_CANCELLATION);

    cras_iodev_list_select_node(CRAS_STREAM_OUTPUT,
                                cras_make_node_id(d1_.info.idx, node1_2.idx));
  }

  {  // Select headphone as the active node.
    EVENTUALLY(EXPECT_EQ, cras_iodev_list_dev_is_enabled(&d1_), 1);
    CLEAR_AND_EVENTUALLY(EXPECT_EQ, cras_observer_notify_nodes_called, 1);

    // Noise Cancellation is blocked.
    EVENTUALLY(ASSERT_EQ, server_state_stub.num_input_nodes, 1);
    EVENTUALLY(EXPECT_EQ, server_state_stub.input_nodes[0].audio_effect, 0);

    cras_iodev_list_select_node(CRAS_STREAM_OUTPUT,
                                cras_make_node_id(d1_.info.idx, node1.idx));
  }

  d1_.format = &fmt_;

  // Setup pinned streams.
  rstream.is_pinned = 1;
  rstream.direction = CRAS_STREAM_OUTPUT;
  rstream.pinned_dev_idx = d1_.info.idx;

  {  // Add pinned stream to d1, which means d1 is opened.
    EVENTUALLY(EXPECT_EQ, 1, cras_iodev_is_open(&d1_));
    CLEAR_AND_EVENTUALLY(EXPECT_EQ, audio_thread_add_stream_called, 1);
    CLEAR_AND_EVENTUALLY(EXPECT_EQ, audio_thread_add_stream_dev, &d1_);
    CLEAR_AND_EVENTUALLY(EXPECT_EQ, audio_thread_add_stream_stream, &rstream);
    CLEAR_AND_EVENTUALLY(EXPECT_EQ, update_active_node_called, 1);

    EVENTUALLY(EXPECT_EQ, rc, 0);

    DL_APPEND(stream_list_get_ret, &rstream);
    rc = stream_add_cb(&rstream);
  }

  {  // Select internal speaker as the active node.
    EVENTUALLY(EXPECT_EQ, cras_iodev_list_dev_is_enabled(&d1_), 1);
    CLEAR_AND_EVENTUALLY(EXPECT_EQ, cras_observer_notify_nodes_called, 1);

    // Noise Cancellation is unblocked because internal speaker is the active
    // node, and the pinned stream is played by internal speaker.
    EVENTUALLY(EXPECT_EQ, cras_iodev_list_dev_is_enabled(&d1_), 1);
    EVENTUALLY(ASSERT_EQ, server_state_stub.num_input_nodes, 1);
    EVENTUALLY(EXPECT_EQ, server_state_stub.input_nodes[0].audio_effect,
               EFFECT_TYPE_NOISE_CANCELLATION);

    cras_iodev_list_select_node(CRAS_STREAM_OUTPUT,
                                cras_make_node_id(d1_.info.idx, node1_2.idx));
  }

  stream_list_has_pinned_stream_ret[d1_.info.idx] = 0;

  {  // Remove pinned stream from d1.
    EVENTUALLY(EXPECT_EQ, rc, 0);

    DL_DELETE(stream_list_get_ret, &rstream);
    rc = stream_rm_cb(&rstream);
  }

  cras_iodev_list_deinit();
}

TEST(SoftvolCurveTest, InputNodeGainToDBFS) {
  for (long gain = 0; gain <= 100; ++gain) {
    long dBFS = convert_dBFS_from_input_node_gain(gain, false);
    EXPECT_EQ(dBFS, (gain - 50) * ((gain > 50) ? 2000 / 50 : 40));
    EXPECT_EQ(gain, convert_input_node_gain_from_dBFS(dBFS, 2000));
  }
}

TEST(SoftvolCurveTest, InternalMicGainToDBFS) {
  cras_system_get_max_internal_mic_gain_return = 1000;
  for (long gain = 0; gain <= 100; ++gain) {
    long dBFS = convert_dBFS_from_input_node_gain(gain, true);
    EXPECT_EQ(dBFS, (gain - 50) * ((gain > 50) ? 1000 / 50 : 40));
    EXPECT_EQ(gain, convert_input_node_gain_from_dBFS(dBFS, 1000));
  }
}

struct OpenIodevTestParam {
  enum CRAS_STREAM_DIRECTION first_iodev_direction;
  enum CRAS_STREAM_DIRECTION second_iodev_direction;
  enum CRAS_IODEV_LAST_OPEN_RESULT expected_result;
  int open_ret;
};

class OpenIodevTestSuite
    : public IodevTests<testing::TestWithParam<OpenIodevTestParam>> {};

TEST_P(OpenIodevTestSuite, IodevLastOpenResult) {
  const auto& param = GetParam();
  struct cras_rstream rstream;
  int rc;

  memset(&rstream, 0, sizeof(rstream));
  rstream.format = fmt_;

  cras_iodev_list_init();

  d1_.direction = param.first_iodev_direction;
  if (d1_.direction == CRAS_STREAM_INPUT) {
    rc = cras_iodev_list_add_input(&d1_);
  } else {
    rc = cras_iodev_list_add_output(&d1_);
  }
  ASSERT_EQ(rc, 0);
  EXPECT_EQ(d1_.info.last_open_result, UNKNOWN);

  d1_.format = &fmt_;
  rstream.direction = param.first_iodev_direction;

  cras_iodev_list_select_node(param.first_iodev_direction,
                              cras_make_node_id(d1_.info.idx, 0));

  {
    EVENTUALLY(EXPECT_EQ, d1_.info.last_open_result, param.expected_result);

    cras_iodev_list_select_node(param.first_iodev_direction,
                                cras_make_node_id(d1_.info.idx, 0));

    cras_iodev_open_ret[cras_iodev_open_called] = param.open_ret;

    if (param.expected_result != UNKNOWN) {
      DL_APPEND(stream_list_get_ret, &rstream);
      stream_add_cb(&rstream);
    }
  }

  cras_iodev_list_deinit();
}

TEST_P(OpenIodevTestSuite, IodevCloseSameLastOpenResult) {
  const auto& param = GetParam();
  struct cras_rstream rstream;
  int rc;

  memset(&rstream, 0, sizeof(rstream));
  rstream.format = fmt_;

  cras_iodev_list_init();

  d1_.direction = param.first_iodev_direction;
  if (d1_.direction == CRAS_STREAM_INPUT) {
    rc = cras_iodev_list_add_input(&d1_);
  } else {
    rc = cras_iodev_list_add_output(&d1_);
  }
  ASSERT_EQ(rc, 0);
  EXPECT_EQ(d1_.info.last_open_result, UNKNOWN);

  d1_.format = &fmt_;
  rstream.direction = param.first_iodev_direction;

  cras_iodev_list_select_node(param.first_iodev_direction,
                              cras_make_node_id(d1_.info.idx, 0));

  {
    EVENTUALLY(EXPECT_EQ, d1_.info.last_open_result, param.expected_result);

    cras_iodev_list_select_node(param.first_iodev_direction,
                                cras_make_node_id(d1_.info.idx, 0));

    cras_iodev_open_ret[cras_iodev_open_called] = param.open_ret;

    if (param.expected_result != UNKNOWN) {
      DL_APPEND(stream_list_get_ret, &rstream);
      stream_add_cb(&rstream);
    }
  }

  {  // Suspend Device. Device is closed, but last open result is same
    EVENTUALLY(EXPECT_EQ, d1_.info.last_open_result, param.expected_result);

    cras_iodev_list_suspend_dev(d1_.info.idx);
  }

  cras_iodev_list_deinit();
}

TEST_P(OpenIodevTestSuite, IodevLastOpenResultWithOtherDevice) {
  const auto& param = GetParam();
  struct cras_rstream rstream1, rstream2;
  int rc;

  memset(&rstream1, 0, sizeof(rstream1));
  memset(&rstream2, 0, sizeof(rstream2));
  rstream1.format = fmt_;
  rstream2.format = fmt_;

  cras_iodev_list_init();

  d1_.direction = param.first_iodev_direction;
  if (d1_.direction == CRAS_STREAM_INPUT) {
    rc = cras_iodev_list_add_input(&d1_);
  } else {
    rc = cras_iodev_list_add_output(&d1_);
  }
  ASSERT_EQ(rc, 0);
  EXPECT_EQ(d1_.info.last_open_result, UNKNOWN);

  d1_.format = &fmt_;

  d2_.direction = param.second_iodev_direction;
  if (d2_.direction == CRAS_STREAM_INPUT) {
    rc = cras_iodev_list_add_input(&d2_);
  } else {
    rc = cras_iodev_list_add_output(&d2_);
  }
  ASSERT_EQ(rc, 0);
  EXPECT_EQ(d2_.info.last_open_result, UNKNOWN);

  d2_.format = &fmt_;

  rstream1.direction = param.first_iodev_direction;
  rstream2.direction = param.second_iodev_direction;

  cras_iodev_list_select_node(param.first_iodev_direction,
                              cras_make_node_id(d1_.info.idx, 0));

  {
    EVENTUALLY(EXPECT_EQ, d1_.info.last_open_result, param.expected_result);

    cras_iodev_list_select_node(param.first_iodev_direction,
                                cras_make_node_id(d1_.info.idx, 0));

    cras_iodev_open_ret[cras_iodev_open_called] = param.open_ret;

    if (param.expected_result != UNKNOWN) {
      DL_APPEND(stream_list_get_ret, &rstream1);
      stream_add_cb(&rstream1);
    }
  }

  {  // d2_ successfully opens
    EVENTUALLY(EXPECT_EQ, d1_.info.last_open_result, param.expected_result);
    EVENTUALLY(EXPECT_EQ, d2_.info.last_open_result, SUCCESS);

    cras_iodev_open_ret[cras_iodev_open_called] = 0;

    cras_iodev_list_select_node(param.second_iodev_direction,
                                cras_make_node_id(d2_.info.idx, 0));

    DL_APPEND(stream_list_get_ret, &rstream2);
    stream_add_cb(&rstream2);
  }

  {  // Suspend device. Device is closed, but last open result is same
    EVENTUALLY(EXPECT_EQ, d1_.info.last_open_result, param.expected_result);
    EVENTUALLY(EXPECT_EQ, d2_.info.last_open_result, SUCCESS);

    cras_iodev_list_suspend_dev(d2_.info.idx);
  }

  {  // Resume device. d2_ fails to open.
    EVENTUALLY(EXPECT_EQ, d1_.info.last_open_result, param.expected_result);
    EVENTUALLY(EXPECT_EQ, d2_.info.last_open_result, FAILURE);

    cras_iodev_open_ret[cras_iodev_open_called] = -5;

    cras_iodev_list_resume_dev(d2_.info.idx);
  }

  cras_iodev_list_deinit();
}

INSTANTIATE_TEST_SUITE_P(
    DeviceDirection,
    OpenIodevTestSuite,
    testing::Values(
        OpenIodevTestParam{CRAS_STREAM_INPUT, CRAS_STREAM_INPUT, UNKNOWN, 0},
        OpenIodevTestParam{CRAS_STREAM_INPUT, CRAS_STREAM_INPUT, SUCCESS, 0},
        OpenIodevTestParam{CRAS_STREAM_INPUT, CRAS_STREAM_INPUT, FAILURE, -5},
        OpenIodevTestParam{CRAS_STREAM_INPUT, CRAS_STREAM_OUTPUT, UNKNOWN, 0},
        OpenIodevTestParam{CRAS_STREAM_INPUT, CRAS_STREAM_OUTPUT, SUCCESS, 0},
        OpenIodevTestParam{CRAS_STREAM_INPUT, CRAS_STREAM_OUTPUT, FAILURE, -5},
        OpenIodevTestParam{CRAS_STREAM_OUTPUT, CRAS_STREAM_INPUT, UNKNOWN, 0},
        OpenIodevTestParam{CRAS_STREAM_OUTPUT, CRAS_STREAM_INPUT, SUCCESS, 0},
        OpenIodevTestParam{CRAS_STREAM_OUTPUT, CRAS_STREAM_INPUT, FAILURE, -5},
        OpenIodevTestParam{CRAS_STREAM_OUTPUT, CRAS_STREAM_OUTPUT, UNKNOWN, 0},
        OpenIodevTestParam{CRAS_STREAM_OUTPUT, CRAS_STREAM_OUTPUT, SUCCESS, 0},
        OpenIodevTestParam{CRAS_STREAM_OUTPUT, CRAS_STREAM_OUTPUT, FAILURE,
                           -5}));

TEST_F(IoDevTestSuite, RequestFloop) {
  ScopedFeaturesOverride feature_override({CrOSLateBootAudioFlexibleLoopback});

  struct cras_floop_pair cfps[NUM_FLOOP_PAIRS_MAX] = {};
  // cras_floop_pair_create fails and returns NULL
  EXPECT_EQ(-ENOMEM, cras_iodev_list_request_floop(nullptr));

  for (int i = 0; i < NUM_FLOOP_PAIRS_MAX; i++) {
    cfps[i].input.info.idx = i;
    cras_floop_pair_create_return = &cfps[i];
    EXPECT_EQ(i, cras_iodev_list_request_floop(nullptr));
  }

  // Should fail with EAGAIN when we already have maximum number of floop pairs
  EXPECT_EQ(-EAGAIN, cras_iodev_list_request_floop(nullptr));

  // Reset the iodev list here to avoid stack-use-after-return of `cfps`.
  cras_iodev_list_reset();
}

}  //  namespace

extern "C" {

// Stubs

struct cras_server_state* cras_system_state_update_begin() {
  return server_state_update_begin_return;
}

void cras_system_state_update_complete() {}

int cras_system_get_mute() {
  return system_get_mute_return;
}

bool cras_system_get_dsp_noise_cancellation_supported() {
  return true;
}

bool cras_system_get_noise_cancellation_enabled() {
  return false;
}

bool cras_system_get_bypass_block_noise_cancellation() {
  return false;
}

bool cras_system_get_noise_cancellation_standalone_mode() {
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
  if (dev->info.idx == SILENT_RECORD_DEVICE ||
      dev->info.idx == SILENT_PLAYBACK_DEVICE) {
    audio_thread_add_open_dev_fallback_called++;
  } else {
    audio_thread_add_open_dev_called++;
  }
  audio_thread_add_open_dev_dev = dev;
  return 0;
}

int audio_thread_rm_open_dev(struct audio_thread* thread,
                             enum CRAS_STREAM_DIRECTION dir,
                             unsigned int dev_idx) {
  if (dev_idx == SILENT_RECORD_DEVICE || dev_idx == SILENT_PLAYBACK_DEVICE) {
    audio_thread_rm_open_dev_fallback_called++;
  } else {
    audio_thread_rm_open_dev_called++;
  }

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
  bool found_normal = false;
  bool found_fallback = false;
  for (unsigned int i = 0; i < num_devs; ++i) {
    if (devs[i]->info.idx == SILENT_RECORD_DEVICE ||
        devs[i]->info.idx == SILENT_PLAYBACK_DEVICE) {
      found_fallback = true;
    } else {
      found_normal = true;
    }
  }
  audio_thread_add_stream_called += found_normal;
  audio_thread_add_stream_fallback_called += found_fallback;
  audio_thread_add_stream_stream = stream;
  audio_thread_add_stream_dev = num_devs ? devs[0] : nullptr;
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
    dev->info.idx = SILENT_HOTWORD_DEVICE;
  } else {
    dev = &mock_empty_iodev[direction];
    dev->info.idx = (direction == CRAS_STREAM_OUTPUT ? SILENT_PLAYBACK_DEVICE
                                                     : SILENT_RECORD_DEVICE);
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
  if (iodev->info.idx == SILENT_RECORD_DEVICE ||
      iodev->info.idx == SILENT_PLAYBACK_DEVICE) {
    iodev->state = CRAS_IODEV_STATE_OPEN;
    cras_iodev_open_fallback_fmt = *fmt;
    iodev->format = &cras_iodev_open_fallback_fmt;
    cras_iodev_open_fallback_called++;
    return 0;
  } else {
    if (cras_iodev_open_ret[cras_iodev_open_called] == 0) {
      iodev->state = CRAS_IODEV_STATE_OPEN;
    }
    cras_iodev_open_fmt[cras_iodev_open_called] = *fmt;
    iodev->format = &cras_iodev_open_fmt[cras_iodev_open_called];
    return cras_iodev_open_ret[cras_iodev_open_called++];
  }
}

int cras_iodev_close(struct cras_iodev* iodev) {
  iodev->state = CRAS_IODEV_STATE_CLOSE;
  if (iodev->info.idx == SILENT_RECORD_DEVICE ||
      iodev->info.idx == SILENT_PLAYBACK_DEVICE) {
    cras_iodev_close_fallback_called++;
  } else {
    cras_iodev_close_called++;
  }
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
bool cras_iodev_is_dsp_aec_use_case(const struct cras_ionode* node) {
  return (node->type == CRAS_NODE_TYPE_INTERNAL_SPEAKER);
}
bool stream_list_has_pinned_stream(struct stream_list* list,
                                   unsigned int dev_idx) {
  return stream_list_has_pinned_stream_ret[dev_idx];
}

struct stream_list* stream_list_create(stream_callback* add_cb,
                                       stream_callback* rm_cb,
                                       stream_create_func* create_cb,
                                       stream_destroy_func* destroy_cb,
                                       stream_callback* list_changed_cb,
                                       struct cras_tm* timer_manager) {
  stream_add_cb = add_cb;
  stream_rm_cb = rm_cb;
  return reinterpret_cast<struct stream_list*>(0xf00);
}

void stream_list_destroy(struct stream_list* list) {}

struct cras_rstream* stream_list_get(struct stream_list* list) {
  return stream_list_get_ret;
}

int stream_list_get_num_output(struct stream_list* list) {
  struct cras_rstream* rstream;
  int num_output_stream = 0;

  DL_FOREACH (stream_list_get(list), rstream) {
    if (rstream->direction == CRAS_STREAM_OUTPUT) {
      num_output_stream++;
    }
  }

  return num_output_stream;
}

int server_stream_create(struct stream_list* stream_list,
                         enum server_stream_type type,
                         unsigned int dev_idx,
                         struct cras_audio_format* format,
                         unsigned int effects) {
  server_stream_create_called++;
  return 0;
}
void server_stream_destroy(struct stream_list* stream_list,
                           enum server_stream_type type,
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
  if (observer_ops) {
    free(observer_ops);
  }
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
  cras_observer_notify_input_node_gain_value = gain;
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

struct cras_floop_pair* cras_floop_pair_create(
    const struct cras_floop_params* params) {
  return cras_floop_pair_create_return;
}

bool cras_floop_pair_match_output_stream(const struct cras_floop_pair* pair,
                                         const struct cras_rstream* stream) {
  return false;
}
bool cras_floop_pair_match_params(const struct cras_floop_pair* pair,
                                  const struct cras_floop_params* params) {
  return false;
}
int cras_server_metrics_set_aec_ref_device_type(struct cras_iodev* iodev) {
  return 0;
}
int cras_server_metrics_stream_add_failure(enum CRAS_STREAM_ADD_ERROR code) {
  return 0;
}

}  // extern "C"
