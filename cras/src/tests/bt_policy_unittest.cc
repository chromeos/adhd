// Copyright 2021 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <gtest/gtest.h>

extern "C" {
#include "cras/src/server/cras_bt_device.h"
#include "cras/src/server/cras_bt_policy.c"  //
#include "cras/src/server/cras_iodev.h"
}
static int cras_iodev_list_suspend_dev_called;
static int cras_iodev_list_resume_dev_called;
static int cras_iodev_list_resume_dev_idx;
static int cras_tm_create_timer_called;
static int cras_tm_cancel_timer_called;
static void (*cras_tm_create_timer_cb)(struct cras_timer* t, void* data);
static void* cras_tm_create_timer_cb_data;
static struct cras_timer* cras_tm_cancel_timer_arg;
static struct cras_timer* cras_tm_create_timer_ret;
static int cras_hfp_ag_start_called;
static int cras_hfp_ag_suspend_connected_device_called;
static int cras_a2dp_start_called;
static int cras_a2dp_suspend_connected_device_called;
static int cras_bt_device_disconnect_called;
static int cras_bt_device_connect_profile_called;
static int cras_bt_device_remove_conflict_called;
static int bt_io_manager_set_nodes_plugged_called;
static bool cras_bt_device_valid_ret;
static bool bt_io_manager_exists_ret;

void ResetStubData() {
  cras_tm_create_timer_called = 0;
  cras_tm_cancel_timer_called = 0;
  cras_iodev_list_suspend_dev_called = 0;
  cras_iodev_list_resume_dev_called = 0;
  cras_hfp_ag_start_called = 0;
  cras_hfp_ag_suspend_connected_device_called = 0;
  cras_a2dp_start_called = 0;
  cras_a2dp_suspend_connected_device_called = 0;
  cras_bt_device_disconnect_called = 0;
  cras_bt_device_connect_profile_called = 0;
  cras_bt_device_remove_conflict_called = 0;
  bt_io_manager_set_nodes_plugged_called = 0;
  cras_tm_create_timer_ret = reinterpret_cast<struct cras_timer*>(0x123);
  cras_bt_device_valid_ret = 1;
  bt_io_manager_exists_ret = 1;
}

// Iodev callback
void update_active_node(struct cras_iodev* iodev,
                        unsigned node_idx,
                        unsigned dev_enabled) {
  return;
}

namespace {
class BtPolicyTestSuite : public testing::Test {
 protected:
  virtual void SetUp() {
    ResetStubData();

    idev.update_active_node = update_active_node;
    idev.info.idx = 0x123;
    odev.update_active_node = update_active_node;
    odev.info.idx = 0x456;

    device.profiles = 0;            // Reset supported profiles
    device.connected_profiles = 0;  // Reset connected profiles

    device.bt_io_mgr = &bt_io_mgr;
    for (int i = 0; i < CRAS_NUM_DIRECTIONS; i++) {
      bt_io_mgr.bt_iodevs[i] = NULL;
    }
    bt_io_mgr.bt_iodevs[CRAS_STREAM_OUTPUT] = &odev;
    bt_io_mgr.bt_iodevs[CRAS_STREAM_INPUT] = &idev;

    btlog = cras_bt_event_log_init();

    msg = CRAS_MAIN_MESSAGE_INIT;
  }
  virtual void TearDown() { cras_bt_event_log_deinit(btlog); }

  struct bt_io_manager bt_io_mgr;
  struct bt_policy_msg msg;
  struct cras_bt_device device;
  struct cras_iodev idev;
  struct cras_iodev odev;
};

TEST_F(BtPolicyTestSuite, HandleMessageWithInvalidDev) {
  /* Pretend the BT device is no longer valid. The message handler will
   * just skip it without any issue. */
  init_bt_profile_switch_msg(&msg, &bt_io_mgr);
  bt_io_manager_exists_ret = 0;
  process_bt_policy_msg(&msg.header, NULL);

  EXPECT_EQ(0, cras_iodev_list_suspend_dev_called);
  EXPECT_EQ(0, cras_iodev_list_resume_dev_called);
  EXPECT_EQ(0, cras_tm_create_timer_called);

  cras_bt_device_valid_ret = 0;
  init_bt_policy_msg(&msg, BT_POLICY_SCHEDULE_SUSPEND, &device, NULL, 200,
                     UNEXPECTED_PROFILE_DROP);
  process_bt_policy_msg(&msg.header, NULL);
  EXPECT_EQ(0, cras_tm_create_timer_called);
  EXPECT_EQ(0, cras_hfp_ag_suspend_connected_device_called);
  EXPECT_EQ(0, cras_a2dp_suspend_connected_device_called);
  EXPECT_EQ(0, cras_bt_device_disconnect_called);
}

TEST_F(BtPolicyTestSuite, SwitchProfile) {
  /* In the typical switch profile case, the associated input and
   * output iodev are suspended and resumed later. */
  EXPECT_EQ(0, cras_iodev_list_suspend_dev_called);
  init_bt_profile_switch_msg(&msg, &bt_io_mgr);

  bt_io_mgr.is_profile_switching = true;

  process_bt_policy_msg(&msg.header, NULL);

  EXPECT_EQ(false, bt_io_mgr.is_profile_switching);

  EXPECT_EQ(2, cras_iodev_list_suspend_dev_called);
  EXPECT_EQ(1, cras_iodev_list_resume_dev_called);
  EXPECT_EQ(idev.info.idx, cras_iodev_list_resume_dev_idx);
  EXPECT_EQ(1, cras_tm_create_timer_called);

  // The output iodev is resumed in a callback
  cras_tm_create_timer_cb(NULL, cras_tm_create_timer_cb_data);
  EXPECT_EQ(2, cras_iodev_list_resume_dev_called);
}

TEST_F(BtPolicyTestSuite, SwitchProfileRepeatedly) {
  init_bt_profile_switch_msg(&msg, &bt_io_mgr);
  process_bt_policy_msg(&msg.header, NULL);
  EXPECT_EQ(2, cras_iodev_list_suspend_dev_called);
  EXPECT_EQ(1, cras_iodev_list_resume_dev_called);
  EXPECT_EQ(idev.info.idx, cras_iodev_list_resume_dev_idx);
  EXPECT_EQ(1, cras_tm_create_timer_called);

  /* Expect repeated profile switch before the schedule callback
   * is executed will cause the timer being cancelled and redo
   * all the suspend/resume and timer creation.
   */
  process_bt_policy_msg(&msg.header, NULL);
  EXPECT_EQ(1, cras_tm_cancel_timer_called);
  EXPECT_EQ(4, cras_iodev_list_suspend_dev_called);
  EXPECT_EQ(2, cras_iodev_list_resume_dev_called);
  EXPECT_EQ(2, cras_tm_create_timer_called);

  cras_tm_create_timer_cb(NULL, cras_tm_create_timer_cb_data);
}

TEST_F(BtPolicyTestSuite, DropHfpBeforeSwitchProfile) {
  /* Test the scenario that for some reason the HFP is dropped but
   * profile switch still went on. The output iodev(A2DP) is
   * expected to still be suspended and resumed.
   */
  device.bt_io_mgr->bt_iodevs[CRAS_STREAM_INPUT] = NULL;
  init_bt_profile_switch_msg(&msg, &bt_io_mgr);
  process_bt_policy_msg(&msg.header, NULL);
  EXPECT_EQ(1, cras_iodev_list_suspend_dev_called);
  EXPECT_EQ(0, cras_iodev_list_resume_dev_called);
  EXPECT_EQ(1, cras_tm_create_timer_called);

  cras_tm_create_timer_cb(NULL, cras_tm_create_timer_cb_data);
  EXPECT_EQ(1, cras_iodev_list_resume_dev_called);
}

TEST_F(BtPolicyTestSuite, DropA2dpWhileSwitchProfile) {
  init_bt_profile_switch_msg(&msg, &bt_io_mgr);
  process_bt_policy_msg(&msg.header, NULL);
  EXPECT_EQ(2, cras_iodev_list_suspend_dev_called);
  EXPECT_EQ(1, cras_iodev_list_resume_dev_called);
  EXPECT_EQ(idev.info.idx, cras_iodev_list_resume_dev_idx);
  EXPECT_EQ(1, cras_tm_create_timer_called);

  /* Test the scenario that for some reason the A2DP is dropped in
   * the middle of profile switch. When the scheduled callback is
   * executed nothing will happen.
   */
  device.bt_io_mgr->bt_iodevs[CRAS_STREAM_OUTPUT] = NULL;
  cras_tm_create_timer_cb(NULL, cras_tm_create_timer_cb_data);
  EXPECT_EQ(2, cras_iodev_list_suspend_dev_called);
  EXPECT_EQ(1, cras_iodev_list_resume_dev_called);
}

TEST_F(BtPolicyTestSuite, RemoveDevWhileSwitchProfile) {
  init_bt_profile_switch_msg(&msg, &bt_io_mgr);
  process_bt_policy_msg(&msg.header, NULL);
  EXPECT_EQ(2, cras_iodev_list_suspend_dev_called);
  EXPECT_EQ(1, cras_iodev_list_resume_dev_called);
  EXPECT_EQ(idev.info.idx, cras_iodev_list_resume_dev_idx);
  EXPECT_EQ(1, cras_tm_create_timer_called);

  /* Test the scenario that for some reaspon the BT device is
   * disconnected in the middle of profile switch. Expect the
   * scheduled timer will be cancelled.
   */
  cras_bt_policy_remove_device(&device);
  EXPECT_EQ(1, cras_tm_cancel_timer_called);
  EXPECT_EQ(2, cras_iodev_list_suspend_dev_called);
  EXPECT_EQ(1, cras_iodev_list_resume_dev_called);
}

TEST_F(BtPolicyTestSuite, ScheduleCancelSuspend) {
  init_bt_policy_msg(&msg, BT_POLICY_SCHEDULE_SUSPEND, &device, NULL, 200,
                     UNEXPECTED_PROFILE_DROP);
  process_bt_policy_msg(&msg.header, NULL);
  EXPECT_EQ(1, cras_tm_create_timer_called);

  // Schedule suspend does nothing if there's a ongoing one.
  init_bt_policy_msg(&msg, BT_POLICY_SCHEDULE_SUSPEND, &device, NULL, 100,
                     HFP_AG_START_FAILURE);
  process_bt_policy_msg(&msg.header, NULL);
  EXPECT_EQ(1, cras_tm_create_timer_called);

  cras_tm_create_timer_cb(NULL, cras_tm_create_timer_cb_data);
  EXPECT_EQ(1, cras_hfp_ag_suspend_connected_device_called);
  EXPECT_EQ(1, cras_a2dp_suspend_connected_device_called);
  EXPECT_EQ(1, cras_bt_device_disconnect_called);

  init_bt_policy_msg(&msg, BT_POLICY_SCHEDULE_SUSPEND, &device, NULL, 200,
                     HFP_AG_START_FAILURE);
  process_bt_policy_msg(&msg.header, NULL);
  EXPECT_EQ(2, cras_tm_create_timer_called);

  init_bt_policy_msg(&msg, BT_POLICY_CANCEL_SUSPEND, &device, NULL, 0, 0);
  process_bt_policy_msg(&msg.header, NULL);
  EXPECT_EQ(1, cras_tm_cancel_timer_called);
}

TEST_F(BtPolicyTestSuite, DevRemoveWithScheduleSuspend) {
  cras_bt_policy_remove_device(&device);
  EXPECT_EQ(0, cras_tm_cancel_timer_called);

  init_bt_policy_msg(&msg, BT_POLICY_SCHEDULE_SUSPEND, &device, NULL, 200,
                     UNEXPECTED_PROFILE_DROP);
  process_bt_policy_msg(&msg.header, NULL);
  EXPECT_EQ(1, cras_tm_create_timer_called);

  cras_bt_policy_remove_device(&device);
  EXPECT_EQ(1, cras_tm_cancel_timer_called);
}

TEST_F(BtPolicyTestSuite, StartConnectionWatchRepeatedly) {
  cras_bt_policy_start_connection_watch(&device);
  EXPECT_EQ(1, cras_tm_create_timer_called);

  cras_bt_policy_start_connection_watch(&device);
  EXPECT_EQ(1, cras_tm_cancel_timer_called);
  EXPECT_EQ(2, cras_tm_create_timer_called);

  cras_bt_policy_stop_connection_watch(&device);
  EXPECT_EQ((void*)conn_watch_policies, (void*)NULL);
}

TEST_F(BtPolicyTestSuite, ConnectionWatchNoAudioProfiles) {
  cras_bt_policy_start_connection_watch(&device);
  EXPECT_EQ(1, cras_tm_create_timer_called);

  // Device doesn't support any profile CRAS cares
  cras_tm_create_timer_cb(NULL, cras_tm_create_timer_cb_data);
  EXPECT_EQ(1, cras_tm_create_timer_called);

  cras_bt_policy_stop_connection_watch(&device);
  EXPECT_EQ((void*)conn_watch_policies, (void*)NULL);
}

TEST_F(BtPolicyTestSuite, ConnectionWatchA2dpAndHfp) {
  cras_bt_device_set_supported_profiles(
      &device,
      CRAS_BT_DEVICE_PROFILE_A2DP_SINK | CRAS_BT_DEVICE_PROFILE_HFP_HANDSFREE);
  cras_bt_policy_start_connection_watch(&device);
  EXPECT_EQ(1, cras_tm_create_timer_called);

  // Expect still waiting for the 1st profile of A2DP and HFP to be connected
  cras_tm_create_timer_cb(NULL, cras_tm_create_timer_cb_data);
  EXPECT_EQ(2, cras_tm_create_timer_called);
  EXPECT_EQ(0, cras_bt_device_connect_profile_called);

  // After A2DP is connected, expect a call is executed to connect HFP
  device.connected_profiles |= CRAS_BT_DEVICE_PROFILE_A2DP_SINK;
  cras_tm_create_timer_cb(NULL, cras_tm_create_timer_cb_data);
  EXPECT_EQ(3, cras_tm_create_timer_called);
  EXPECT_EQ(1, cras_bt_device_connect_profile_called);

  device.connected_profiles |= CRAS_BT_DEVICE_PROFILE_HFP_HANDSFREE;
  cras_tm_create_timer_cb(NULL, cras_tm_create_timer_cb_data);
  EXPECT_EQ(3, cras_tm_create_timer_called);
  EXPECT_EQ(1, cras_bt_device_remove_conflict_called);
  EXPECT_EQ(1, cras_hfp_ag_start_called);
  EXPECT_EQ(1, cras_a2dp_start_called);
  EXPECT_EQ(1, bt_io_manager_set_nodes_plugged_called);

  EXPECT_EQ((void*)conn_watch_policies, (void*)NULL);
}

TEST_F(BtPolicyTestSuite, ConnectionWatchHfpOnly) {
  cras_bt_device_set_supported_profiles(&device,
                                        CRAS_BT_DEVICE_PROFILE_HFP_HANDSFREE);
  cras_bt_policy_start_connection_watch(&device);
  EXPECT_EQ(1, cras_tm_create_timer_called);

  cras_tm_create_timer_cb(NULL, cras_tm_create_timer_cb_data);
  EXPECT_EQ(2, cras_tm_create_timer_called);

  device.connected_profiles |= CRAS_BT_DEVICE_PROFILE_HFP_HANDSFREE;
  cras_tm_create_timer_cb(NULL, cras_tm_create_timer_cb_data);
  EXPECT_EQ(2, cras_tm_create_timer_called);
  EXPECT_EQ(0, cras_bt_device_connect_profile_called);

  EXPECT_EQ(1, cras_bt_device_remove_conflict_called);
  EXPECT_EQ(1, cras_hfp_ag_start_called);
  EXPECT_EQ(0, cras_a2dp_start_called);
  EXPECT_EQ(1, bt_io_manager_set_nodes_plugged_called);

  EXPECT_EQ((void*)conn_watch_policies, (void*)NULL);
}

TEST_F(BtPolicyTestSuite, ConnectionWatchA2dpOnly) {
  cras_bt_device_set_supported_profiles(&device,
                                        CRAS_BT_DEVICE_PROFILE_A2DP_SINK);
  cras_bt_policy_start_connection_watch(&device);
  EXPECT_EQ(1, cras_tm_create_timer_called);

  cras_tm_create_timer_cb(NULL, cras_tm_create_timer_cb_data);
  EXPECT_EQ(2, cras_tm_create_timer_called);

  device.connected_profiles |= CRAS_BT_DEVICE_PROFILE_A2DP_SINK;
  cras_tm_create_timer_cb(NULL, cras_tm_create_timer_cb_data);
  EXPECT_EQ(2, cras_tm_create_timer_called);
  EXPECT_EQ(0, cras_bt_device_connect_profile_called);

  EXPECT_EQ(1, cras_bt_device_remove_conflict_called);
  EXPECT_EQ(0, cras_hfp_ag_start_called);
  EXPECT_EQ(1, cras_a2dp_start_called);
  EXPECT_EQ(1, bt_io_manager_set_nodes_plugged_called);

  EXPECT_EQ((void*)conn_watch_policies, (void*)NULL);
}

TEST_F(BtPolicyTestSuite, ConnectionWatchTimeout) {
  cras_bt_device_set_supported_profiles(
      &device,
      CRAS_BT_DEVICE_PROFILE_A2DP_SINK | CRAS_BT_DEVICE_PROFILE_HFP_HANDSFREE);
  cras_bt_policy_start_connection_watch(&device);
  EXPECT_EQ(1, cras_tm_create_timer_called);
  EXPECT_EQ((void*)conn_watch_policies, (void*)cras_tm_create_timer_cb_data);

  for (unsigned int i = 0; i < CONN_WATCH_MAX_RETRIES; i++) {
    cras_tm_create_timer_cb(NULL, cras_tm_create_timer_cb_data);
    EXPECT_EQ(0, cras_a2dp_start_called);
    EXPECT_EQ(0, cras_hfp_ag_start_called);

    /* Expect connection watch is armed repeatedly until in the last retry
     * a suspend policy is arranged instead. */
    EXPECT_EQ(i + 2, cras_tm_create_timer_called);
    if (i < CONN_WATCH_MAX_RETRIES - 1) {
      EXPECT_EQ((void*)conn_watch_policies,
                (void*)cras_tm_create_timer_cb_data);
    } else {
      EXPECT_EQ((void*)suspend_policies, (void*)cras_tm_create_timer_cb_data);
    }
  }
  cras_bt_policy_stop_connection_watch(&device);
}

}  // namespace

extern "C" {

struct cras_bt_event_log* btlog;

// From cras_main_message
int cras_main_message_send(struct cras_main_message* msg) {
  return 0;
}

// From cras_system_state
struct cras_tm* cras_system_state_get_tm() {
  return NULL;
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
  ASSERT_NE(t, (void*)NULL);
  cras_tm_cancel_timer_called++;
  cras_tm_cancel_timer_arg = t;
}

// From cras_iodev_list
void cras_iodev_list_suspend_dev(unsigned int dev_idx) {
  cras_iodev_list_suspend_dev_called++;
}

void cras_iodev_list_resume_dev(unsigned int dev_idx) {
  cras_iodev_list_resume_dev_called++;
  cras_iodev_list_resume_dev_idx = dev_idx;
}

int cras_hfp_ag_start(struct cras_bt_device* device) {
  cras_hfp_ag_start_called++;
  return 0;
}

void cras_hfp_ag_suspend_connected_device(struct cras_bt_device* device) {
  cras_hfp_ag_suspend_connected_device_called++;
}

void cras_a2dp_start(struct cras_bt_device* device) {
  cras_a2dp_start_called++;
}

void cras_a2dp_suspend_connected_device(struct cras_bt_device* device) {
  cras_a2dp_suspend_connected_device_called++;
}

bool bt_io_manager_exists(struct bt_io_manager* target) {
  return bt_io_manager_exists_ret;
}

int cras_bt_device_disconnect(DBusConnection* conn,
                              struct cras_bt_device* device) {
  cras_bt_device_disconnect_called++;
  return 0;
}

void cras_bt_device_remove_conflict(struct cras_bt_device* device) {
  cras_bt_device_remove_conflict_called++;
}

int cras_bt_device_set_supported_profiles(struct cras_bt_device* device,
                                          unsigned int profiles) {
  device->profiles |= profiles;
  return 0;
}

int cras_bt_device_connect_profile(DBusConnection* conn,
                                   struct cras_bt_device* device,
                                   const char* uuid) {
  cras_bt_device_connect_profile_called++;
  return 0;
}
bool cras_bt_device_valid(const struct cras_bt_device* target) {
  return cras_bt_device_valid_ret;
}
void bt_io_manager_set_nodes_plugged(struct bt_io_manager* mgr, int plugged) {
  bt_io_manager_set_nodes_plugged_called++;
}

}  // extern "C"
