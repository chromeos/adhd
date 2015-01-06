// Copyright (c) 2014 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <gtest/gtest.h>

extern "C" {
#include "cras_bt_io.h"
#include "cras_bt_device.h"
#include "cras_iodev.h"

#define FAKE_OBJ_PATH "/obj/path"
}

static struct cras_iodev *cras_bt_io_create_profile_ret;
static struct cras_iodev *cras_bt_io_append_btio_val;
static int cras_bt_io_has_dev_ret;
static unsigned int cras_bt_io_create_called;
static unsigned int cras_bt_io_append_called;
static unsigned int cras_bt_io_remove_called;
static unsigned int cras_bt_io_destroy_called;
static struct cras_iodev *audio_thread_add_active_dev_dev;
static unsigned int audio_thread_add_active_dev_called;
static unsigned int audio_thread_rm_active_dev_called;
static int audio_thread_rm_active_dev_rets[CRAS_NUM_DIRECTIONS];
static audio_thread* iodev_get_thread_return;
static enum cras_bt_device_profile cras_bt_io_create_profile_val;
static enum cras_bt_device_profile cras_bt_io_append_profile_val;
static unsigned int cras_bt_io_try_remove_ret;

static void (*cras_system_add_select_fd_callback)(void *data);
static void *cras_system_add_select_fd_callback_data;

void ResetStubData() {
  cras_bt_io_has_dev_ret = 0;
  cras_bt_io_create_called = 0;
  cras_bt_io_append_called = 0;
  cras_bt_io_remove_called = 0;
  cras_bt_io_destroy_called = 0;
  cras_bt_io_try_remove_ret = 0;
  audio_thread_add_active_dev_called = 0;
  audio_thread_rm_active_dev_called = 0;
  for (int dir = 0; dir < CRAS_NUM_DIRECTIONS; dir++)
    audio_thread_rm_active_dev_rets[dir] = 0;
}

namespace {

class BtDeviceTestSuite : public testing::Test {
  protected:
    virtual void SetUp() {
      ResetStubData();
      bt_iodev1.direction = CRAS_STREAM_OUTPUT;
      bt_iodev1.is_open = is_open;
      bt_iodev1.update_active_node = update_active_node;
      bt_iodev2.direction = CRAS_STREAM_INPUT;
      bt_iodev2.is_open = is_open;
      bt_iodev2.update_active_node = update_active_node;
      d1_.direction = CRAS_STREAM_OUTPUT;
      d1_.is_open = is_open;
      d1_.update_active_node = update_active_node;
      d2_.direction = CRAS_STREAM_OUTPUT;
      d2_.is_open = is_open;
      d2_.update_active_node = update_active_node;
      d3_.direction = CRAS_STREAM_INPUT;
      d3_.is_open = is_open;
      d3_.update_active_node = update_active_node;
    }
    static int is_open(const cras_iodev* iodev) {
      return is_open_;
    }
    static void update_active_node(struct cras_iodev *iodev) {
    }

    struct cras_iodev bt_iodev1;
    struct cras_iodev bt_iodev2;
    struct cras_iodev d3_;
    struct cras_iodev d2_;
    struct cras_iodev d1_;
    static int is_open_;
};

int BtDeviceTestSuite::is_open_;

TEST(BtDeviceSuite, CreateBtDevice) {
  struct cras_bt_device *device;

  device = cras_bt_device_create(FAKE_OBJ_PATH);
  EXPECT_NE((void *)NULL, device);

  device = cras_bt_device_get(FAKE_OBJ_PATH);
  EXPECT_NE((void *)NULL, device);

  cras_bt_device_destroy(device);
  device = cras_bt_device_get(FAKE_OBJ_PATH);
  EXPECT_EQ((void *)NULL, device);
}

TEST_F(BtDeviceTestSuite, AppendRmIodev) {
  struct cras_bt_device *device;
  device = cras_bt_device_create(FAKE_OBJ_PATH);
  bt_iodev1.nodes = reinterpret_cast<struct cras_ionode*>(0x123);
  cras_bt_io_create_profile_ret = &bt_iodev1;
  cras_bt_device_append_iodev(device, &d1_,
      CRAS_BT_DEVICE_PROFILE_A2DP_SOURCE);
  EXPECT_EQ(1, cras_bt_io_create_called);
  EXPECT_EQ(0, cras_bt_io_append_called);
  EXPECT_EQ(CRAS_BT_DEVICE_PROFILE_A2DP_SOURCE,
            cras_bt_io_create_profile_val);

  cras_bt_device_append_iodev(device, &d2_,
      CRAS_BT_DEVICE_PROFILE_HFP_AUDIOGATEWAY);
  EXPECT_EQ(1, cras_bt_io_create_called);
  EXPECT_EQ(1, cras_bt_io_append_called);
  EXPECT_EQ(CRAS_BT_DEVICE_PROFILE_HFP_AUDIOGATEWAY,
  	    cras_bt_io_append_profile_val);
  EXPECT_EQ(&bt_iodev1, cras_bt_io_append_btio_val);

  /* Test HFP disconnected and switch to A2DP. */
  cras_bt_io_has_dev_ret = 1;
  cras_bt_io_try_remove_ret = CRAS_BT_DEVICE_PROFILE_A2DP_SOURCE;
  audio_thread_rm_active_dev_rets[CRAS_STREAM_OUTPUT] = 0;
  cras_bt_device_set_active_profile(
      device, CRAS_BT_DEVICE_PROFILE_HFP_AUDIOGATEWAY);
  cras_bt_device_rm_iodev(device, &d2_);
  EXPECT_EQ(1, cras_bt_io_remove_called);
  EXPECT_EQ(1, audio_thread_rm_active_dev_called);
  EXPECT_EQ(1, audio_thread_add_active_dev_called);

  /* Test A2DP disconnection will cause bt_io destroy. */
  cras_bt_io_try_remove_ret = 0;
  cras_bt_device_rm_iodev(device, &d1_);
  EXPECT_EQ(1, cras_bt_io_remove_called);
  EXPECT_EQ(1, cras_bt_io_destroy_called);
}

TEST_F(BtDeviceTestSuite, SwitchProfile) {
  struct cras_bt_device *device;

  ResetStubData();
  device = cras_bt_device_create(FAKE_OBJ_PATH);
  cras_bt_io_create_profile_ret = &bt_iodev1;
  cras_bt_device_append_iodev(device, &d1_,
      CRAS_BT_DEVICE_PROFILE_A2DP_SOURCE);
  cras_bt_io_create_profile_ret = &bt_iodev2;
  cras_bt_device_append_iodev(device, &d3_,
      CRAS_BT_DEVICE_PROFILE_HFP_AUDIOGATEWAY);

  cras_bt_device_start_monitor();
  cras_bt_device_switch_profile_on_open(device, &bt_iodev1);

  /* Two bt iodevs were all active. */
  audio_thread_rm_active_dev_rets[CRAS_STREAM_INPUT] = 0;
  audio_thread_rm_active_dev_rets[CRAS_STREAM_OUTPUT] = 0;
  cras_system_add_select_fd_callback(cras_system_add_select_fd_callback_data);
  EXPECT_EQ(2, audio_thread_rm_active_dev_called);
  EXPECT_EQ(2, audio_thread_add_active_dev_called);

  /* One bt iodev was active, the other was not. */
  cras_bt_device_switch_profile_on_open(device, &bt_iodev2);
  audio_thread_rm_active_dev_rets[CRAS_STREAM_OUTPUT] = 0;
  audio_thread_rm_active_dev_rets[CRAS_STREAM_INPUT] = -1;
  cras_system_add_select_fd_callback(cras_system_add_select_fd_callback_data);
  EXPECT_EQ(4, audio_thread_rm_active_dev_called);
  EXPECT_EQ(4, audio_thread_add_active_dev_called);

  /* Output bt iodev wasn't active, close the active input iodev. */
  cras_bt_device_switch_profile_on_close(device, &bt_iodev2);
  audio_thread_rm_active_dev_rets[CRAS_STREAM_OUTPUT] = -1;
  audio_thread_rm_active_dev_rets[CRAS_STREAM_INPUT] = 0;
  cras_system_add_select_fd_callback(cras_system_add_select_fd_callback_data);
  EXPECT_EQ(6, audio_thread_rm_active_dev_called);
  EXPECT_EQ(5, audio_thread_add_active_dev_called);
}

/* Stubs */
extern "C" {

/* From bt_io */
struct cras_iodev *cras_bt_io_create(
        struct cras_bt_device *device,
				struct cras_iodev *dev,
				enum cras_bt_device_profile profile)
{
  cras_bt_io_create_called++;
  cras_bt_io_create_profile_val = profile;
  return cras_bt_io_create_profile_ret;
}
void cras_bt_io_destroy(struct cras_iodev *bt_iodev)
{
  cras_bt_io_destroy_called++;
}
int cras_bt_io_has_dev(struct cras_iodev *bt_iodev, struct cras_iodev *dev)
{
  return cras_bt_io_has_dev_ret;
}
int cras_bt_io_append(struct cras_iodev *bt_iodev,
		      struct cras_iodev *dev,
		      enum cras_bt_device_profile profile)
{
  cras_bt_io_append_called++;
  cras_bt_io_append_profile_val = profile;
  cras_bt_io_append_btio_val = bt_iodev;
  return 0;
}
int cras_bt_io_on_profile(struct cras_iodev *bt_iodev,
                          enum cras_bt_device_profile profile)
{
  return 0;
}
unsigned int cras_bt_io_try_remove(struct cras_iodev *bt_iodev,
           struct cras_iodev *dev)
{
  return cras_bt_io_try_remove_ret;
}
int cras_bt_io_remove(struct cras_iodev *bt_iodev,
		                  struct cras_iodev *dev)
{
  cras_bt_io_remove_called++;
  return 0;
}

/* From bt_adapter */
struct cras_bt_adapter *cras_bt_adapter_get(const char *object_path)
{
  return NULL;
}
const char *cras_bt_adapter_address(const struct cras_bt_adapter *adapter)
{
  return NULL;
}

/* From bt_profile */
void cras_bt_profile_on_device_disconnected(struct cras_bt_device *device)
{
}

/* From hfp_ag_profile */
struct hfp_slc_handle *cras_hfp_ag_get_slc(struct cras_bt_device *device)
{
  return NULL;
}

/* From hfp_slc */
int hfp_event_speaker_gain(struct hfp_slc_handle *handle, int gain)
{
  return 0;
}

/* From audio_thread */
int audio_thread_add_active_dev(struct audio_thread *thread,
         struct cras_iodev *dev)
{
  audio_thread_add_active_dev_dev = dev;
  audio_thread_add_active_dev_called++;
  return 0;
}

int audio_thread_rm_active_dev(struct audio_thread *thread,
        struct cras_iodev *dev)
{
  audio_thread_rm_active_dev_called++;
  return audio_thread_rm_active_dev_rets[dev->direction];
}

/* From iodev_list */
struct audio_thread* cras_iodev_list_get_audio_thread() {
  return iodev_get_thread_return;
}

int cras_system_add_select_fd(int fd,
            void (*callback)(void *data),
            void *callback_data)
{
  cras_system_add_select_fd_callback = callback;
  cras_system_add_select_fd_callback_data = callback_data;
  return 0;
}

} // extern "C"
} // namespace

int main(int argc, char **argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}


