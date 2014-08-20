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
static enum cras_bt_device_profile cras_bt_io_create_profile_val;
static enum cras_bt_device_profile cras_bt_io_append_profile_val;

void ResetStubData() {
  cras_bt_io_has_dev_ret = 0;
  cras_bt_io_create_called = 0;
  cras_bt_io_append_called = 0;
  cras_bt_io_remove_called = 0;
  cras_bt_io_destroy_called = 0;
}

namespace {

class BtDeviceTestSuite : public testing::Test {
  protected:
    virtual void SetUp() {
      ResetStubData();
      d1_.direction = CRAS_STREAM_OUTPUT;
      d1_.is_open = is_open;
      d2_.direction = CRAS_STREAM_OUTPUT;
      d2_.is_open = is_open;
    }
    static int is_open(const cras_iodev* iodev) {
      return is_open_;
    }

    struct cras_iodev bt_iodev1;
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

  cras_bt_io_has_dev_ret = 1;
  cras_bt_device_rm_iodev(device, &d2_);
  EXPECT_EQ(1, cras_bt_io_remove_called);
  bt_iodev1.nodes = NULL;
  cras_bt_device_rm_iodev(device, &d1_);
  EXPECT_EQ(2, cras_bt_io_remove_called);
  EXPECT_EQ(1, cras_bt_io_destroy_called);
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
} // extern "C"
} // namespace

int main(int argc, char **argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}


