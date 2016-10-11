// Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stdio.h>
#include <gtest/gtest.h>

extern "C" {
#include "audio_thread.h"
#include "cras_iodev.h"
#include "cras_iodev_list.h"
#include "cras_rstream.h"
#include "cras_system_state.h"
#include "cras_tm.h"
#include "stream_list.h"
#include "utlist.h"
}

namespace {

struct cras_server_state server_state_stub;
struct cras_server_state *server_state_update_begin_return;

/* Data for stubs. */
static cras_alert_cb suspend_cb;
static unsigned int register_suspend_cb_called;
static unsigned int remove_suspend_cb_called;
static unsigned int cras_system_get_suspended_val;
static int add_stream_called;
static int rm_stream_called;
static unsigned int set_node_attr_called;
static cras_iodev *audio_thread_remove_streams_active_dev;
static cras_iodev *audio_thread_set_active_dev_val;
static int audio_thread_set_active_dev_called;
static cras_iodev *audio_thread_add_open_dev_dev;
static int audio_thread_add_open_dev_called;
static int audio_thread_rm_open_dev_called;
static struct audio_thread thread;
static size_t node_left_right_swapped_cb_called;
static size_t node_volume_cb_called;
static size_t node_gain_cb_called;
static struct cras_iodev loopback_input;
static int cras_iodev_close_called;
static struct cras_iodev *cras_iodev_close_dev;
static struct cras_iodev dummy_empty_iodev[2];
static stream_callback *stream_add_cb;
static stream_callback *stream_rm_cb;
static struct cras_rstream *stream_list_get_ret;
static int audio_thread_drain_stream_return;
static int audio_thread_drain_stream_called;
static void (*cras_tm_timer_cb)(struct cras_timer *t, void *data);
static struct timespec clock_gettime_retspec;
static struct cras_iodev *device_enabled_dev;
static struct cras_iodev *device_disabled_dev;
static void *device_enabled_cb_data;
static struct cras_rstream *audio_thread_add_stream_stream;
static struct cras_iodev *audio_thread_add_stream_dev;
static int audio_thread_add_stream_called;
static unsigned update_active_node_called;
static struct cras_iodev *update_active_node_iodev_val[5];
static unsigned update_active_node_node_idx_val[5];
static unsigned update_active_node_dev_enabled_val[5];
static size_t cras_observer_add_called;
static size_t cras_observer_remove_called;
static size_t cras_observer_notify_nodes_called;
static size_t cras_observer_notify_active_node_called;
static size_t cras_observer_notify_output_node_volume_called;
static size_t cras_observer_notify_node_left_right_swapped_called;
static size_t cras_observer_notify_input_node_gain_called;

/* Callback in iodev_list. */
void node_left_right_swapped_cb(cras_node_id_t, int)
{
  node_left_right_swapped_cb_called++;
}

void node_volume_cb(cras_node_id_t, int)
{
  node_volume_cb_called++;
}

void node_gain_cb(cras_node_id_t, int)
{
  node_gain_cb_called++;
}

void dummy_update_active_node(struct cras_iodev *iodev,
                              unsigned node_idx,
                              unsigned dev_enabled) {
}

class IoDevTestSuite : public testing::Test {
  protected:
    virtual void SetUp() {
      cras_iodev_list_reset();

      cras_iodev_close_called = 0;
      stream_list_get_ret = 0;
      audio_thread_drain_stream_return = 0;
      audio_thread_drain_stream_called = 0;

      sample_rates_[0] = 44100;
      sample_rates_[1] = 48000;
      sample_rates_[2] = 0;

      channel_counts_[0] = 2;
      channel_counts_[1] = 0;

      memset(&d1_, 0, sizeof(d1_));
      memset(&d2_, 0, sizeof(d2_));
      memset(&d3_, 0, sizeof(d3_));

      memset(&node1, 0, sizeof(node1));
      memset(&node2, 0, sizeof(node2));
      memset(&node3, 0, sizeof(node3));

      d1_.set_volume = NULL;
      d1_.set_mute = NULL;
      d1_.set_capture_gain = NULL;
      d1_.set_capture_mute = NULL;
      d1_.update_supported_formats = NULL;
      d1_.update_active_node = update_active_node;
      d1_.format = NULL;
      d1_.direction = CRAS_STREAM_OUTPUT;
      d1_.info.idx = -999;
      d1_.nodes = &node1;
      d1_.active_node = &node1;
      strcpy(d1_.info.name, "d1");
      d1_.supported_rates = sample_rates_;
      d1_.supported_channel_counts = channel_counts_;
      d2_.set_volume = NULL;
      d2_.set_mute = NULL;
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
      d3_.set_mute = NULL;
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
      loopback_input.set_mute = NULL;
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

      /* Reset stub data. */
      register_suspend_cb_called = 0;
      remove_suspend_cb_called = 0;
      add_stream_called = 0;
      rm_stream_called = 0;
      set_node_attr_called = 0;
      audio_thread_rm_open_dev_called = 0;
      audio_thread_add_open_dev_called = 0;
      audio_thread_set_active_dev_called = 0;
      node_left_right_swapped_cb_called = 0;
      node_volume_cb_called = 0;
      node_gain_cb_called = 0;
      audio_thread_add_stream_called = 0;
      update_active_node_called = 0;
      cras_observer_add_called = 0;
      cras_observer_remove_called = 0;
      cras_observer_notify_nodes_called = 0;
      cras_observer_notify_active_node_called = 0;
      cras_observer_notify_output_node_volume_called = 0;
      cras_observer_notify_node_left_right_swapped_called = 0;
      cras_observer_notify_input_node_gain_called = 0;
    }

    static void set_volume_1(struct cras_iodev* iodev) {
      set_volume_1_called_++;
    }

    static void set_mute_1(struct cras_iodev* iodev) {
      set_mute_1_called_++;
    }

    static void set_capture_gain_1(struct cras_iodev* iodev) {
      set_capture_gain_1_called_++;
    }

    static void set_capture_mute_1(struct cras_iodev* iodev) {
      set_capture_mute_1_called_++;
    }

    static void update_active_node(struct cras_iodev *iodev,
                                   unsigned node_idx,
                                   unsigned dev_enabled) {
      int i = update_active_node_called++ % 5;
      update_active_node_iodev_val[i] = iodev;
      update_active_node_node_idx_val[i] = node_idx;
      update_active_node_dev_enabled_val[i] = dev_enabled;
    }

    struct cras_iodev d1_;
    struct cras_iodev d2_;
    struct cras_iodev d3_;
    size_t sample_rates_[3];
    size_t channel_counts_[2];
    static int set_volume_1_called_;
    static int set_mute_1_called_;
    static int set_capture_gain_1_called_;
    static int set_capture_mute_1_called_;
    struct cras_ionode node1, node2, node3;
};

int IoDevTestSuite::set_volume_1_called_;
int IoDevTestSuite::set_mute_1_called_;
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
  struct cras_rstream *stream_list = NULL;
  int rc;

  memset(&rstream, 0, sizeof(rstream));
  memset(&rstream2, 0, sizeof(rstream2));
  memset(&rstream3, 0, sizeof(rstream3));

  cras_iodev_list_init();

  d1_.direction = CRAS_STREAM_OUTPUT;
  rc = cras_iodev_list_add_output(&d1_);
  ASSERT_EQ(0, rc);

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

  cras_system_get_suspended_val = 1;
  audio_thread_rm_open_dev_called = 0;
  suspend_cb(NULL, NULL);
  EXPECT_EQ(1, audio_thread_rm_open_dev_called);

  /* Test disable/enable dev won't cause add_stream to audio_thread. */
  audio_thread_add_stream_called = 0;
  cras_iodev_list_disable_dev(&d1_);
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
  cras_system_get_suspended_val = 0;
  stream_list_get_ret = stream_list;
  suspend_cb(NULL, NULL);
  EXPECT_EQ(1, audio_thread_add_open_dev_called);
  EXPECT_EQ(2, audio_thread_add_stream_called);
  EXPECT_EQ(&rstream3, audio_thread_add_stream_stream);

  cras_iodev_list_deinit();
  EXPECT_EQ(3, cras_observer_notify_active_node_called);
}

TEST_F(IoDevTestSuite, SelectNode) {
  struct cras_rstream rstream, rstream2, rstream3;
  struct cras_rstream *stream_list = NULL;
  int rc;

  memset(&rstream, 0, sizeof(rstream));
  memset(&rstream2, 0, sizeof(rstream2));
  memset(&rstream3, 0, sizeof(rstream3));

  cras_iodev_list_init();

  d1_.direction = CRAS_STREAM_OUTPUT;
  rc = cras_iodev_list_add_output(&d1_);
  ASSERT_EQ(0, rc);

  d2_.direction = CRAS_STREAM_OUTPUT;
  rc = cras_iodev_list_add_output(&d2_);
  ASSERT_EQ(0, rc);

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

  stream_list_get_ret = stream_list;
  cras_iodev_list_select_node(CRAS_STREAM_OUTPUT,
      cras_make_node_id(d2_.info.idx, 1));
  EXPECT_EQ(6, audio_thread_add_stream_called);
  EXPECT_EQ(2, cras_observer_notify_active_node_called);
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

  EXPECT_EQ(1, update_active_node_called);
  EXPECT_EQ(&d2_, update_active_node_iodev_val[0]);
  EXPECT_EQ(1, update_active_node_node_idx_val[0]);
  EXPECT_EQ(1, update_active_node_dev_enabled_val[0]);

  /* Fake the active node idx on d2_, and later assert this node is
   * called for update_active_node when d2_ disabled. */
  d2_.active_node->idx = 2;
  cras_iodev_list_select_node(CRAS_STREAM_OUTPUT,
      cras_make_node_id(d1_.info.idx, 0));

  EXPECT_EQ(3, update_active_node_called);
  EXPECT_EQ(&d2_, update_active_node_iodev_val[1]);
  EXPECT_EQ(&d1_, update_active_node_iodev_val[2]);
  EXPECT_EQ(2, update_active_node_node_idx_val[1]);
  EXPECT_EQ(0, update_active_node_node_idx_val[2]);
  EXPECT_EQ(0, update_active_node_dev_enabled_val[1]);
  EXPECT_EQ(1, update_active_node_dev_enabled_val[2]);
  EXPECT_EQ(2, cras_observer_notify_active_node_called);
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
  cras_iodev_list_select_node(CRAS_STREAM_OUTPUT,
      cras_make_node_id(2, 1));
  EXPECT_EQ(0, d1_.is_enabled);
  EXPECT_EQ(2, cras_observer_notify_active_node_called);
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
  struct cras_iodev_info *dev_info;
  int rc;

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
}

static void device_enabled_cb(struct cras_iodev *dev, int enabled,
                              void *cb_data)
{
  if (enabled)
    device_enabled_dev = dev;
  else
    device_disabled_dev = dev;
  device_enabled_cb_data = cb_data;
}

// Test enable/disable an iodev.
TEST_F(IoDevTestSuite, EnableDisableDevice) {
  EXPECT_EQ(0, cras_iodev_list_add_output(&d1_));

  EXPECT_EQ(0, cras_iodev_list_set_device_enabled_callback(
      device_enabled_cb, (void *)0xABCD));

  // Enable a device.
  cras_iodev_list_enable_dev(&d1_);
  EXPECT_EQ(&d1_, device_enabled_dev);
  EXPECT_EQ((void *)0xABCD, device_enabled_cb_data);
  EXPECT_EQ(&d1_, cras_iodev_list_get_first_enabled_iodev(CRAS_STREAM_OUTPUT));

  // Disable a device.
  cras_iodev_list_disable_dev(&d1_);
  EXPECT_EQ(&d1_, device_disabled_dev);
  EXPECT_EQ((void *)0xABCD, device_enabled_cb_data);

  EXPECT_EQ(-EEXIST, cras_iodev_list_set_device_enabled_callback(
      device_enabled_cb, (void *)0xABCD));
  EXPECT_EQ(2, cras_observer_notify_active_node_called);
}

// Test adding/removing an input dev to the list.
TEST_F(IoDevTestSuite, AddRemoveInput) {
  struct cras_iodev_info *dev_info;
  int rc, i;
  uint32_t found_mask;

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
      EXPECT_EQ(0, (found_mask & (1 << idx)));
      found_mask |= (1 << idx);
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

  rc = cras_iodev_list_add_input(&d1_);
  EXPECT_EQ(0, rc);
  EXPECT_GE(d1_.info.idx, 0);
  rc = cras_iodev_list_add_input(&d2_);
  EXPECT_EQ(0, rc);
  EXPECT_GE(d2_.info.idx, 1);

  EXPECT_EQ(0, cras_iodev_list_rm_input(&d1_));
  EXPECT_EQ(0, cras_iodev_list_rm_input(&d2_));
}

// Test removing the last input.
TEST_F(IoDevTestSuite, RemoveLastInput) {
  struct cras_iodev_info *dev_info;
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
  cras_iodev_list_set_node_left_right_swapped_callbacks(
      node_left_right_swapped_cb);
  cras_iodev_list_notify_node_left_right_swapped(&ionode);
  EXPECT_EQ(1, node_left_right_swapped_cb_called);
  EXPECT_EQ(1, cras_observer_notify_node_left_right_swapped_called);
}

// Test callback function for volume and gain are set and called.
TEST_F(IoDevTestSuite, VolumeGainCallback) {

  struct cras_iodev iodev;
  struct cras_ionode ionode;
  memset(&iodev, 0, sizeof(iodev));
  memset(&ionode, 0, sizeof(ionode));
  ionode.dev = &iodev;
  cras_iodev_list_set_node_volume_callbacks(node_volume_cb, node_gain_cb);
  cras_iodev_list_notify_node_volume(&ionode);
  cras_iodev_list_notify_node_capture_gain(&ionode);
  EXPECT_EQ(1, node_volume_cb_called);
  EXPECT_EQ(1, node_gain_cb_called);
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
  EXPECT_EQ(0, set_node_attr_called);

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
  EXPECT_EQ(0, set_node_attr_called);

  // Mismatch id
  rc = cras_iodev_list_set_node_attr(cras_make_node_id(d1_.info.idx, 2),
                                     IONODE_ATTR_PLUGGED, 1);
  EXPECT_LT(rc, 0);
  EXPECT_EQ(0, set_node_attr_called);

  // Correct device id and node id
  rc = cras_iodev_list_set_node_attr(cras_make_node_id(d1_.info.idx, 1),
                                     IONODE_ATTR_PLUGGED, 1);
  EXPECT_EQ(rc, 0);
  EXPECT_EQ(1, set_node_attr_called);
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
}

TEST_F(IoDevTestSuite, DrainTimerCancel) {
  int rc;
  struct cras_rstream rstream;

  memset(&rstream, 0, sizeof(rstream));

  cras_iodev_list_init();

  d1_.direction = CRAS_STREAM_OUTPUT;
  rc = cras_iodev_list_add_output(&d1_);
  EXPECT_EQ(0, rc);

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

}

TEST_F(IoDevTestSuite, AddRemovePinnedStream) {
  struct cras_rstream rstream;

  cras_iodev_list_init();

  // Add 2 output devices.
  d1_.direction = CRAS_STREAM_OUTPUT;
  EXPECT_EQ(0, cras_iodev_list_add_output(&d1_));
  d2_.direction = CRAS_STREAM_OUTPUT;
  EXPECT_EQ(0, cras_iodev_list_add_output(&d2_));

  // Setup pinned stream.
  memset(&rstream, 0, sizeof(rstream));
  rstream.is_pinned = 1;
  rstream.pinned_dev_idx = d1_.info.idx;

  // Add pinned stream to d1.
  EXPECT_EQ(0, stream_add_cb(&rstream));
  EXPECT_EQ(1, audio_thread_add_stream_called);
  EXPECT_EQ(&d1_, audio_thread_add_stream_dev);
  EXPECT_EQ(&rstream, audio_thread_add_stream_stream);

  // Enable d2, check pinned stream is not added to d2.
  cras_iodev_list_enable_dev(&d2_);
  EXPECT_EQ(1, audio_thread_add_stream_called);

  // Remove pinned stream from d1, check d1 is closed after stream removed.
  EXPECT_EQ(0, stream_rm_cb(&rstream));
  EXPECT_EQ(1, cras_iodev_close_called);
  EXPECT_EQ(&d1_, cras_iodev_close_dev);
}

}  //  namespace

int main(int argc, char **argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}

extern "C" {

// Stubs

struct cras_server_state *cras_system_state_update_begin() {
  return server_state_update_begin_return;
}

void cras_system_state_update_complete() {
}

int cras_system_register_suspend_cb(cras_alert_cb cb, void *arg)
{
  suspend_cb = cb;
  register_suspend_cb_called++;
  return 0;
}

int cras_system_remove_suspend_cb(cras_alert_cb cb, void *arg)
{
  remove_suspend_cb_called++;
  return 0;
}

int cras_system_get_suspended()
{
  return cras_system_get_suspended_val;
}

struct audio_thread *audio_thread_create() {
  return &thread;
}

int audio_thread_start(struct audio_thread *thread) {
  return 0;
}

void audio_thread_destroy(struct audio_thread *thread) {
}

int audio_thread_set_active_dev(struct audio_thread *thread,
                                 struct cras_iodev *dev) {
  audio_thread_set_active_dev_called++;
  audio_thread_set_active_dev_val = dev;
  return 0;
}

void audio_thread_remove_streams(struct audio_thread *thread,
				 enum CRAS_STREAM_DIRECTION dir) {
  audio_thread_remove_streams_active_dev = audio_thread_set_active_dev_val;
}

int audio_thread_add_open_dev(struct audio_thread *thread,
				 struct cras_iodev *dev)
{
  audio_thread_add_open_dev_dev = dev;
  audio_thread_add_open_dev_called++;
  return 0;
}

int audio_thread_rm_open_dev(struct audio_thread *thread,
                               struct cras_iodev *dev)
{
  audio_thread_rm_open_dev_called++;
  return 0;
}

int audio_thread_add_stream(struct audio_thread *thread,
                            struct cras_rstream *stream,
                            struct cras_iodev **devs,
                            unsigned int num_devs)
{
  audio_thread_add_stream_called++;
  audio_thread_add_stream_stream = stream;
  audio_thread_add_stream_dev = (num_devs ? devs[0] : NULL);
  return 0;
}

int audio_thread_disconnect_stream(struct audio_thread *thread,
                                   struct cras_rstream *stream,
                                   struct cras_iodev *iodev)
{
  return 0;
}

int audio_thread_drain_stream(struct audio_thread *thread,
                              struct cras_rstream *stream)
{
	audio_thread_drain_stream_called++;
	return audio_thread_drain_stream_return;
}

void set_node_volume(struct cras_ionode *node, int value)
{
  struct cras_iodev *dev = node->dev;
  unsigned int volume;

  if (dev->direction != CRAS_STREAM_OUTPUT)
    return;

  volume = (unsigned int)std::min(value, 100);
  node->volume = volume;
  if (dev->set_volume)
    dev->set_volume(dev);

  cras_iodev_list_notify_node_volume(node);
}

int cras_iodev_set_node_attr(struct cras_ionode *ionode,
                             enum ionode_attr attr, int value)
{
  set_node_attr_called++;

  switch (attr) {
  case IONODE_ATTR_PLUGGED:
    // plug_node(ionode, value);
    break;
  case IONODE_ATTR_VOLUME:
    set_node_volume(ionode, value);
    break;
  case IONODE_ATTR_CAPTURE_GAIN:
    // set_node_capture_gain(ionode, value);
    break;
  default:
    return -EINVAL;
  }

  return 0;
}

struct cras_iodev *empty_iodev_create(enum CRAS_STREAM_DIRECTION direction) {
  dummy_empty_iodev[direction].direction = direction;
  dummy_empty_iodev[direction].update_active_node = dummy_update_active_node;
  if (dummy_empty_iodev[direction].active_node == NULL) {
    struct cras_ionode *node = (struct cras_ionode *)calloc(1, sizeof(*node));
    dummy_empty_iodev[direction].active_node = node;
  }
  return &dummy_empty_iodev[direction];
}

struct cras_iodev *test_iodev_create(enum CRAS_STREAM_DIRECTION direction,
                                     enum TEST_IODEV_TYPE type) {
  return NULL;
}

void test_iodev_command(struct cras_iodev *iodev,
                        enum CRAS_TEST_IODEV_CMD command,
                        unsigned int data_len,
                        const uint8_t *data) {
}

struct cras_iodev *loopback_iodev_create(enum CRAS_LOOPBACK_TYPE type) {
  return &loopback_input;
}

void loopback_iodev_destroy(struct cras_iodev *iodev) {
}

int cras_iodev_open(struct cras_iodev *iodev, unsigned int cb_level)
{
  iodev->state = CRAS_IODEV_STATE_OPEN;
  return 0;
}

int cras_iodev_close(struct cras_iodev *iodev) {
  iodev->state = CRAS_IODEV_STATE_CLOSE;
  cras_iodev_close_called++;
  cras_iodev_close_dev = iodev;
  return 0;
}

int cras_iodev_set_format(struct cras_iodev *iodev,
                          const struct cras_audio_format *fmt) {
  return 0;
}

struct stream_list *stream_list_create(stream_callback *add_cb,
                                       stream_callback *rm_cb,
                                       stream_create_func *create_cb,
                                       stream_destroy_func *destroy_cb,
				       struct cras_tm *timer_manager) {
  stream_add_cb = add_cb;
  stream_rm_cb = rm_cb;
  return reinterpret_cast<stream_list *>(0xf00);
}

void stream_list_destroy(struct stream_list *list) {
}

struct cras_rstream *stream_list_get(struct stream_list *list) {
  return stream_list_get_ret;
}

int cras_rstream_create(struct cras_rstream_config *config,
                        struct cras_rstream **stream_out) {
  return 0;
}

void cras_rstream_destroy(struct cras_rstream *rstream) {
}

struct cras_tm *cras_system_state_get_tm() {
  return NULL;
}

struct cras_timer *cras_tm_create_timer(
                struct cras_tm *tm,
                unsigned int ms,
                void (*cb)(struct cras_timer *t, void *data),
                void *cb_data) {
  cras_tm_timer_cb = cb;
  return reinterpret_cast<struct cras_timer *>(0x404);
}

void cras_tm_cancel_timer(struct cras_tm *tm, struct cras_timer *t) {
}

void cras_fmt_conv_destroy(struct cras_fmt_conv *conv)
{
}

struct cras_fmt_conv *cras_channel_remix_conv_create(
    unsigned int num_channels, const float *coefficient)
{
  return NULL;
}

void cras_channel_remix_convert(struct cras_fmt_conv *conv,
    uint8_t *in_buf, size_t frames)
{
}

struct cras_observer_client *cras_observer_add(
      const struct cras_observer_ops *ops,
      void *context)
{
  cras_observer_add_called++;
  return reinterpret_cast<struct cras_observer_client *>(0x55);
}

void cras_observer_remove(struct cras_observer_client *client)
{
  cras_observer_remove_called++;
}

void cras_observer_notify_nodes(void) {
  cras_observer_notify_nodes_called++;
}

void cras_observer_notify_active_node(enum CRAS_STREAM_DIRECTION direction,
				      cras_node_id_t node_id)
{
  cras_observer_notify_active_node_called++;
}

void cras_observer_notify_output_node_volume(cras_node_id_t node_id,
					     int32_t volume)
{
  cras_observer_notify_output_node_volume_called++;
}

void cras_observer_notify_node_left_right_swapped(cras_node_id_t node_id,
						  int swapped)
{
  cras_observer_notify_node_left_right_swapped_called++;
}

void cras_observer_notify_input_node_gain(cras_node_id_t node_id,
					  int32_t gain)
{
  cras_observer_notify_input_node_gain_called++;
}

//  From librt.
int clock_gettime(clockid_t clk_id, struct timespec *tp) {
  tp->tv_sec = clock_gettime_retspec.tv_sec;
  tp->tv_nsec = clock_gettime_retspec.tv_nsec;
  return 0;
}

}  // extern "C"
