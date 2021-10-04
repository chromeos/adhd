// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <gtest/gtest.h>

extern "C" {
#include "cras_bt_device.h"
#include "cras_bt_policy.c" /* */
#include "cras_iodev.h"
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

void ResetStubData() {
  cras_tm_create_timer_called = 0;
  cras_tm_cancel_timer_called = 0;
  cras_iodev_list_suspend_dev_called = 0;
  cras_iodev_list_resume_dev_called = 0;
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

    for (int i = 0; i < CRAS_NUM_DIRECTIONS; i++)
      device.bt_iodevs[i] = NULL;
    device.bt_iodevs[CRAS_STREAM_OUTPUT] = &odev;
    device.bt_iodevs[CRAS_STREAM_INPUT] = &idev;
  }
  virtual void TearDown() {}

  struct cras_bt_device device;
  struct cras_iodev idev;
  struct cras_iodev odev;
};

TEST_F(BtPolicyTestSuite, SwitchProfile) {
  /* In the typical switch profile case, the associated input and
   * output iodev are suspended and resumed later. */
  switch_profile(&device, &idev);
  EXPECT_EQ(2, cras_iodev_list_suspend_dev_called);
  EXPECT_EQ(1, cras_iodev_list_resume_dev_called);
  EXPECT_EQ(idev.info.idx, cras_iodev_list_resume_dev_idx);
  EXPECT_EQ(1, cras_tm_create_timer_called);

  /* The output iodev is resumed in a callback */
  cras_tm_create_timer_cb(NULL, cras_tm_create_timer_cb_data);
  EXPECT_EQ(2, cras_iodev_list_resume_dev_called);
}

TEST_F(BtPolicyTestSuite, SwitchProfileRepeatedly) {
  switch_profile(&device, &idev);
  EXPECT_EQ(2, cras_iodev_list_suspend_dev_called);
  EXPECT_EQ(1, cras_iodev_list_resume_dev_called);
  EXPECT_EQ(idev.info.idx, cras_iodev_list_resume_dev_idx);
  EXPECT_EQ(1, cras_tm_create_timer_called);

  /* Expect repeated profile switch before the schedule callback
   * is executed will cause the timer being cancelled and redo
   * all the suspend/resume and timer creation.
   */
  switch_profile(&device, &idev);
  EXPECT_EQ(1, cras_tm_cancel_timer_called);
  EXPECT_EQ(4, cras_iodev_list_suspend_dev_called);
  EXPECT_EQ(2, cras_iodev_list_resume_dev_called);
  EXPECT_EQ(2, cras_tm_create_timer_called);
}

TEST_F(BtPolicyTestSuite, DropHfpBeforeSwitchProfile) {
  /* Test the scenario that for some reason the HFP is dropped but
   * profile switch still went on. The output iodev(A2DP) is
   * expected to still be suspended and resumed.
   */
  device.bt_iodevs[CRAS_STREAM_INPUT] = NULL;
  switch_profile(&device, &idev);
  EXPECT_EQ(1, cras_iodev_list_suspend_dev_called);
  EXPECT_EQ(0, cras_iodev_list_resume_dev_called);
  EXPECT_EQ(1, cras_tm_create_timer_called);

  cras_tm_create_timer_cb(NULL, cras_tm_create_timer_cb_data);
  EXPECT_EQ(1, cras_iodev_list_resume_dev_called);
}

TEST_F(BtPolicyTestSuite, DropA2dpWhileSwitchProfile) {
  switch_profile(&device, &idev);
  EXPECT_EQ(2, cras_iodev_list_suspend_dev_called);
  EXPECT_EQ(1, cras_iodev_list_resume_dev_called);
  EXPECT_EQ(idev.info.idx, cras_iodev_list_resume_dev_idx);
  EXPECT_EQ(1, cras_tm_create_timer_called);

  /* Test the scenario that for some reason the A2DP is dropped in
   * the middle of profile switch. When the scheduled callback is
   * executed nothing will happen.
   */
  device.bt_iodevs[CRAS_STREAM_OUTPUT] = NULL;
  cras_tm_create_timer_cb(NULL, cras_tm_create_timer_cb_data);
  EXPECT_EQ(2, cras_iodev_list_suspend_dev_called);
  EXPECT_EQ(1, cras_iodev_list_resume_dev_called);
}

TEST_F(BtPolicyTestSuite, RemoveDevWhileSwitchProfile) {
  switch_profile(&device, &idev);
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

}  // namespace

extern "C" {

/* From cras_main_message */
int cras_main_message_send(struct cras_main_message* msg) {
  return 0;
}

/* From cras_system_state */
struct cras_tm* cras_system_state_get_tm() {
  return NULL;
}

/* From cras_tm */
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

/* From cras_iodev_list */
void cras_iodev_list_suspend_dev(unsigned int dev_idx) {
  cras_iodev_list_suspend_dev_called++;
}

void cras_iodev_list_resume_dev(unsigned int dev_idx) {
  cras_iodev_list_resume_dev_called++;
  cras_iodev_list_resume_dev_idx = dev_idx;
}
}  // extern "C"

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
