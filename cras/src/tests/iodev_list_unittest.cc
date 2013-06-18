// Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stdio.h>
#include <gtest/gtest.h>

extern "C" {
#include "cras_iodev.h"
#include "cras_iodev_list.h"
#include "cras_rstream.h"
#include "cras_system_state.h"
#include "utlist.h"
}

namespace {

struct cras_server_state server_state_stub;
struct cras_server_state *server_state_update_begin_return;

/* Data for stubs. */
static cras_alert_cb volume_changed_cb;
static void* volume_changed_arg;
static unsigned int register_volume_changed_cb_called;
static unsigned int remove_volume_changed_cb_called;
static cras_alert_cb mute_changed_cb;
static void* mute_changed_arg;
static unsigned int register_mute_changed_cb_called;
static unsigned int remove_mute_changed_cb_called;
static cras_alert_cb capture_gain_changed_cb;
static void* capture_gain_changed_arg;
static unsigned int register_capture_gain_changed_cb_called;
static unsigned int remove_capture_gain_changed_cb_called;
static cras_alert_cb capture_mute_changed_cb;
static void* capture_mute_changed_arg;
static unsigned int register_capture_mute_changed_cb_called;
static unsigned int remove_capture_mute_changed_cb_called;
static int add_stream_called;
static int rm_stream_called;
static unsigned int set_node_attr_called;
static int cras_alert_create_called;
static int cras_alert_destroy_called;
static int cras_alert_pending_called;

class IoDevTestSuite : public testing::Test {
  protected:
    virtual void SetUp() {
      cras_iodev_list_reset();

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
      d1_.is_open = is_open;
      d1_.update_supported_formats = NULL;
      d1_.set_as_default = NULL;
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
      d2_.is_open = is_open;
      d2_.update_supported_formats = NULL;
      d2_.set_as_default = NULL;
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
      d3_.is_open = is_open;
      d3_.update_supported_formats = NULL;
      d3_.set_as_default = NULL;
      d3_.format = NULL;
      d3_.direction = CRAS_STREAM_OUTPUT;
      d3_.info.idx = -999;
      d3_.nodes = &node3;
      d3_.active_node = &node3;
      strcpy(d3_.info.name, "d3");
      d3_.supported_rates = sample_rates_;
      d3_.supported_channel_counts = channel_counts_;

      server_state_update_begin_return = &server_state_stub;

      /* Reset stub data. */
      register_volume_changed_cb_called = 0;
      remove_volume_changed_cb_called = 0;
      register_capture_gain_changed_cb_called = 0;
      remove_capture_gain_changed_cb_called = 0;
      register_mute_changed_cb_called = 0;
      remove_mute_changed_cb_called = 0;
      register_capture_mute_changed_cb_called = 0;
      remove_capture_mute_changed_cb_called = 0;
      add_stream_called = 0;
      rm_stream_called = 0;
      set_node_attr_called = 0;
      cras_alert_create_called = 0;
      cras_alert_destroy_called = 0;
      cras_alert_pending_called = 0;
      is_open_ = 0;
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

    static void set_as_default(struct cras_iodev *iodev)
    {
      default_dev_to_set_ = iodev;
    }

    static int is_open(const cras_iodev* iodev) {
      return is_open_;
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
    static cras_iodev *default_dev_to_set_;
    static int is_open_;
    struct cras_ionode node1, node2, node3;
};

int IoDevTestSuite::set_volume_1_called_;
int IoDevTestSuite::set_mute_1_called_;
int IoDevTestSuite::set_capture_gain_1_called_;
int IoDevTestSuite::set_capture_mute_1_called_;
cras_iodev *IoDevTestSuite::default_dev_to_set_;
int IoDevTestSuite::is_open_;

// Check that Init registers a volume changed callback. */
TEST_F(IoDevTestSuite, InitSetup) {
  cras_iodev_list_init();
  EXPECT_EQ(1, register_volume_changed_cb_called);
  EXPECT_EQ(1, register_mute_changed_cb_called);
  EXPECT_EQ(1, register_capture_gain_changed_cb_called);
  EXPECT_EQ(1, register_capture_mute_changed_cb_called);
  cras_iodev_list_deinit();
  EXPECT_EQ(1, remove_volume_changed_cb_called);
  EXPECT_EQ(1, remove_mute_changed_cb_called);
  EXPECT_EQ(1, remove_capture_gain_changed_cb_called);
  EXPECT_EQ(1, remove_capture_mute_changed_cb_called);
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
  struct cras_iodev *idev, *odev;
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

  rc = cras_get_iodev_for_stream_type(CRAS_STREAM_TYPE_DEFAULT,
                                      CRAS_STREAM_OUTPUT,
                                      &idev,
                                      &odev);
  EXPECT_EQ(0, rc);
  ASSERT_NE(static_cast<void*>(0), odev);
  EXPECT_EQ(static_cast<void*>(0), idev);
  EXPECT_EQ(d1_.info.idx, odev->info.idx);

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
}

// Test adding/removing an input dev to the list.
TEST_F(IoDevTestSuite, AddRemoveInput) {
  struct cras_iodev_info *dev_info;
  int rc, i;
  uint32_t found_mask;

  d1_.direction = CRAS_STREAM_INPUT;
  d2_.direction = CRAS_STREAM_INPUT;

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

  // Passing null should return the number of outputs.
  rc = cras_iodev_list_get_inputs(NULL);
  EXPECT_EQ(2, rc);
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
  // Should be 0 devs now.
  rc = cras_iodev_list_get_inputs(&dev_info);
  EXPECT_EQ(0, rc);
  free(dev_info);
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

TEST_F(IoDevTestSuite, UnifiedFillsBoth) {
  struct cras_iodev *idev, *odev;
  int rc;

  d1_.direction = CRAS_STREAM_OUTPUT;
  d2_.direction = CRAS_STREAM_INPUT;

  rc = cras_iodev_list_add_output(&d1_);
  EXPECT_EQ(0, rc);
  rc = cras_iodev_list_add_input(&d2_);
  EXPECT_EQ(0, rc);

  // unified stream should fill both input and output devices.
  rc = cras_get_iodev_for_stream_type(CRAS_STREAM_TYPE_DEFAULT,
                                      CRAS_STREAM_UNIFIED,
                                      &idev,
                                      &odev);
  EXPECT_EQ(0, rc);
  EXPECT_EQ(&d1_, odev);
  EXPECT_EQ(&d2_, idev);

  rc = cras_iodev_list_rm_output(&d1_);
  EXPECT_EQ(0, rc);
  rc = cras_iodev_list_rm_input(&d2_);
  EXPECT_EQ(0, rc);
}

// Test removing the last input.
TEST_F(IoDevTestSuite, RemoveLastInput) {
  struct cras_iodev_info *dev_info;
  struct cras_iodev *idev, *odev;
  int rc;

  d1_.direction = CRAS_STREAM_INPUT;
  d2_.direction = CRAS_STREAM_INPUT;

  rc = cras_iodev_list_add_input(&d1_);
  EXPECT_EQ(0, rc);
  rc = cras_iodev_list_add_input(&d2_);
  EXPECT_EQ(0, rc);

  // Default should fall back to most recently added device.
  rc = cras_get_iodev_for_stream_type(CRAS_STREAM_TYPE_DEFAULT,
                                      CRAS_STREAM_INPUT,
                                      &idev,
                                      &odev);
  EXPECT_EQ(0, rc);
  EXPECT_EQ(&d1_, idev);

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
  // Should be 0 devs now.
  rc = cras_iodev_list_get_inputs(&dev_info);
  EXPECT_EQ(0, rc);
}

// Test default device behavior when add/remove devices.
TEST_F(IoDevTestSuite, SetAsDefaultDevice) {
  int rc;

  d1_.set_as_default = set_as_default;
  d2_.set_as_default = set_as_default;

  rc = cras_iodev_list_add_output(&d1_);
  EXPECT_EQ(0, rc);
  rc = cras_iodev_list_add_output(&d2_);
  EXPECT_EQ(0, rc);

  cras_iodev_move_stream_type(CRAS_STREAM_TYPE_DEFAULT, d1_.info.idx);
  EXPECT_EQ(default_dev_to_set_, &d1_);

  cras_iodev_move_stream_type(CRAS_STREAM_TYPE_DEFAULT, d2_.info.idx);
  EXPECT_EQ(default_dev_to_set_, &d2_);

  // Remove then add a non-default device, should be set to default.
  rc = cras_iodev_list_rm_output(&d1_);
  EXPECT_EQ(0, rc);

  rc = cras_iodev_list_add_output(&d1_);
  EXPECT_EQ(0, rc);
  cras_iodev_move_stream_type(CRAS_STREAM_TYPE_DEFAULT, d1_.info.idx);
  EXPECT_EQ(default_dev_to_set_, &d1_);

  // Second device in queue should become default when default removed.
  rc = cras_iodev_list_rm_output(&d1_);
  EXPECT_EQ(0, rc);
  rc = cras_iodev_list_rm_output(&d2_);
  EXPECT_EQ(0, rc);
}

// Test volume callbacks for default output.
TEST_F(IoDevTestSuite, VolumeCallbacks) {
  int rc;

  cras_iodev_list_init();
  ASSERT_EQ(1, register_volume_changed_cb_called);
  ASSERT_TRUE(volume_changed_cb);

  rc = cras_iodev_list_add_output(&d1_);
  EXPECT_EQ(0, rc);

  // Check that callback isn't called if not open and no callback set.
  is_open_ = 0;
  set_volume_1_called_ = 0;
  d1_.set_volume = NULL;
  volume_changed_cb(volume_changed_arg);
  EXPECT_EQ(0, set_volume_1_called_);

  // Check that callback isn't called if open and no callback is set.
  is_open_ = 1;
  set_volume_1_called_ = 0;
  d1_.set_volume = NULL;
  volume_changed_cb(volume_changed_arg);
  EXPECT_EQ(0, set_volume_1_called_);

  // Check that it is called if there is a callback and iodev is active.
  is_open_ = 1;
  set_volume_1_called_ = 0;
  d1_.set_volume = set_volume_1;
  volume_changed_cb(volume_changed_arg);
  EXPECT_EQ(1, set_volume_1_called_);

  is_open_ = 0;
  rc = cras_iodev_list_rm_output(&d1_);
  EXPECT_EQ(0, rc);
}

// Test mute callbacks for default output.
TEST_F(IoDevTestSuite, MuteCallbacks) {
  int rc;

  cras_iodev_list_init();
  ASSERT_EQ(1, register_mute_changed_cb_called);
  ASSERT_TRUE(mute_changed_cb);

  rc = cras_iodev_list_add_output(&d1_);
  EXPECT_EQ(0, rc);

  // Check that callback isn't called if not open.
  is_open_ = 0;
  set_mute_1_called_ = 0;
  d1_.set_mute = set_mute_1;
  mute_changed_cb(mute_changed_arg);
  EXPECT_EQ(0, set_mute_1_called_);

  // Check that callback isn't called if no callback is set.
  is_open_ = 1;
  set_mute_1_called_ = 0;
  d1_.set_mute = NULL;
  mute_changed_cb(mute_changed_arg);
  EXPECT_EQ(0, set_mute_1_called_);

  // Check that it is called if there is a callback and iodev is active.
  is_open_ = 1;
  set_mute_1_called_ = 0;
  d1_.set_mute = set_mute_1;
  mute_changed_cb(mute_changed_arg);
  EXPECT_EQ(1, set_mute_1_called_);

  is_open_ = 0;
  rc = cras_iodev_list_rm_output(&d1_);
  EXPECT_EQ(0, rc);
}

// Test capture gain callbacks for default output.
TEST_F(IoDevTestSuite, CaptureGainCallbacks) {
  int rc;

  cras_iodev_list_init();
  ASSERT_EQ(1, register_capture_gain_changed_cb_called);
  ASSERT_TRUE(capture_gain_changed_cb);

  d1_.direction = CRAS_STREAM_INPUT;
  rc = cras_iodev_list_add_input(&d1_);
  EXPECT_EQ(0, rc);

  // Check that callback isn't called if not open.
  is_open_ = 0;
  set_capture_gain_1_called_ = 0;
  d1_.set_capture_gain = set_capture_gain_1;
  capture_gain_changed_cb(capture_gain_changed_arg);
  EXPECT_EQ(0, set_capture_gain_1_called_);

  // Check that callback isn't called if no callback is set.
  is_open_ = 1;
  set_capture_gain_1_called_ = 0;
  d1_.set_capture_gain = NULL;
  capture_gain_changed_cb(capture_gain_changed_arg);
  EXPECT_EQ(0, set_capture_gain_1_called_);

  // Check that it is called if there is a callback and iodev is active.
  is_open_ = 1;
  set_capture_gain_1_called_ = 0;
  d1_.set_capture_gain = set_capture_gain_1;
  capture_gain_changed_cb(capture_gain_changed_arg);
  EXPECT_EQ(1, set_capture_gain_1_called_);

  is_open_ = 0;
  rc = cras_iodev_list_rm_input(&d1_);
  EXPECT_EQ(0, rc);
}

// Test capture mute callbacks for default output.
TEST_F(IoDevTestSuite, CaptureMuteCallbacks) {
  int rc;

  cras_iodev_list_init();
  ASSERT_EQ(1, register_capture_mute_changed_cb_called);
  ASSERT_TRUE(capture_mute_changed_cb);

  d1_.direction = CRAS_STREAM_INPUT;
  rc = cras_iodev_list_add_input(&d1_);
  EXPECT_EQ(0, rc);

  // Check that callback isn't called if not open.
  is_open_ = 0;
  set_capture_mute_1_called_ = 0;
  d1_.set_capture_mute = set_capture_mute_1;
  capture_mute_changed_cb(capture_mute_changed_arg);
  EXPECT_EQ(0, set_capture_mute_1_called_);

  // Check that callback isn't called if no callback is set.
  is_open_ = 1;
  set_capture_mute_1_called_ = 0;
  d1_.set_capture_mute = NULL;
  capture_mute_changed_cb(capture_mute_changed_arg);
  EXPECT_EQ(0, set_capture_mute_1_called_);

  // Check that it is called if there is a callback and iodev is active.
  is_open_ = 1;
  set_capture_mute_1_called_ = 0;
  d1_.set_capture_mute = set_capture_mute_1;
  capture_mute_changed_cb(capture_mute_changed_arg);
  EXPECT_EQ(1, set_capture_mute_1_called_);

  is_open_ = 0;
  rc = cras_iodev_list_rm_input(&d1_);
  EXPECT_EQ(0, rc);
}

// Test nodes changed notification is sent.
TEST_F(IoDevTestSuite, NodesChangedNotification) {
  EXPECT_EQ(0, cras_alert_create_called);
  cras_iodev_list_init();
  /* One for nodes changed and one for active node changed */
  EXPECT_EQ(2, cras_alert_create_called);

  EXPECT_EQ(0, cras_alert_pending_called);
  cras_iodev_list_notify_nodes_changed();
  EXPECT_EQ(1, cras_alert_pending_called);

  EXPECT_EQ(0, cras_alert_destroy_called);
  cras_iodev_list_deinit();
  EXPECT_EQ(2, cras_alert_destroy_called);
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

}  //  namespace

int main(int argc, char **argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}

extern "C" {

// Stubs

void cras_rstream_send_client_reattach(const struct cras_rstream *stream) {
}

struct cras_server_state *cras_system_state_update_begin() {
  return server_state_update_begin_return;
}

void cras_system_state_update_complete() {
}

int cras_system_register_volume_changed_cb(cras_alert_cb cb, void *arg) {
  volume_changed_cb = cb;
  volume_changed_arg = arg;
  register_volume_changed_cb_called++;
  return 0;
}

int cras_system_remove_volume_changed_cb(cras_alert_cb cb, void *arg) {
  remove_volume_changed_cb_called++;
  return 0;
}

int cras_system_register_mute_changed_cb(cras_alert_cb cb, void *arg) {
  mute_changed_cb = cb;
  mute_changed_arg = arg;
  register_mute_changed_cb_called++;
  return 0;
}

int cras_system_remove_mute_changed_cb(cras_alert_cb cb, void *arg) {
  remove_mute_changed_cb_called++;
  return 0;
}

int cras_system_register_capture_gain_changed_cb(cras_alert_cb cb, void *arg) {
  capture_gain_changed_cb = cb;
  capture_gain_changed_arg = arg;
  register_capture_gain_changed_cb_called++;
  return 0;
}

int cras_system_remove_capture_gain_changed_cb(cras_alert_cb cb, void *arg) {
  remove_capture_gain_changed_cb_called++;
  return 0;
}

int cras_system_register_capture_mute_changed_cb(cras_alert_cb cb, void *arg) {
  capture_mute_changed_cb = cb;
  capture_mute_changed_arg = arg;
  register_capture_mute_changed_cb_called++;
  return 0;
}

int cras_system_remove_capture_mute_changed_cb(cras_alert_cb cb, void *arg) {
  remove_capture_mute_changed_cb_called++;
  return 0;
}

struct cras_alert *cras_alert_create(cras_alert_prepare prepare) {
  cras_alert_create_called++;
  return NULL;
}

int cras_alert_add_callback(struct cras_alert *alert, cras_alert_cb cb,
                            void *arg) {
  return 0;
}

int cras_alert_rm_callback(struct cras_alert *alert, cras_alert_cb cb,
                           void *arg) {
  return 0;
}

void cras_alert_pending(struct cras_alert *alert) {
  cras_alert_pending_called++;
}

void cras_alert_destroy(struct cras_alert *alert) {
  cras_alert_destroy_called++;
}

struct audio_thread *audio_thread_create() {
  return NULL;
}

int audio_thread_start(struct audio_thread *thread) {
  return 0;
}

void audio_thread_destroy(struct audio_thread *thread) {
}

void audio_thread_set_output_dev(struct audio_thread *thread,
                                 struct cras_iodev *odev) {
}

void audio_thread_set_input_dev(struct audio_thread *thread,
                                struct cras_iodev *idev) {
}

void audio_thread_remove_streams(struct audio_thread *thread) {
}

int cras_ionode_better(struct cras_ionode *a, struct cras_ionode *b)
{
  if (a->priority > b->priority)
    return 1;
  if (a->priority < b->priority)
    return 0;

  return 0;
}

int cras_iodev_set_node_attr(struct cras_ionode *ionode,
			     enum ionode_attr attr, int value)
{
  set_node_attr_called++;
  return 0;
}

}  // extern "C"
