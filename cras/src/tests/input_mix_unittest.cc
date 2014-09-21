// Copyright (c) 2014 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stdio.h>
#include <gtest/gtest.h>

extern "C" {
#include "cras_types.h"
#include "input_mix.h"
}

namespace {

class DevMixTestSuite : public testing::Test{
  protected:
    virtual void SetUp() {
    }

    virtual void TearDown() {
    }

};

TEST_F(DevMixTestSuite, DevMixCreate) {
  dev_mix *dm = dev_mix_create(1024);
  EXPECT_NE(static_cast<dev_mix *>(NULL), dm);
  dev_mix_destroy(dm);
}

TEST_F(DevMixTestSuite, DevMixAddRmDev) {
  dev_mix *dm = dev_mix_create(1024);
  int rc;

  rc = dev_mix_add_dev(dm, 0xf00);
  EXPECT_EQ(0, rc);
  rc = dev_mix_add_dev(dm, 0xf00);
  EXPECT_NE(0, rc);

  rc = dev_mix_rm_dev(dm, 0xf00);
  EXPECT_EQ(0, rc);
  rc = dev_mix_rm_dev(dm, 0xf00);
  EXPECT_NE(0, rc);

  dev_mix_destroy(dm);
}

TEST_F(DevMixTestSuite, DevMixAddManyDevs) {
  dev_mix *dm = dev_mix_create(1024);
  int rc;

  for (unsigned int i = 0; i < INITIAL_DEV_SIZE; i++) {
    rc = dev_mix_add_dev(dm, 0xf00 + i);
    EXPECT_EQ(0, rc);
  }

  rc = dev_mix_add_dev(dm, 0xf00 + INITIAL_DEV_SIZE);
  EXPECT_EQ(0, rc);

  dev_mix_destroy(dm);
}

TEST_F(DevMixTestSuite, OneDev) {
  dev_mix *dm = dev_mix_create(1024);
  int rc;

  rc = dev_mix_add_dev(dm, 0xf00);
  EXPECT_EQ(0, rc);

  dev_mix_frames_added(dm, 0xf00, 500);
  EXPECT_EQ(500, dev_mix_get_new_write_point(dm));

  dev_mix_frames_added(dm, 0xf00, 500);
  EXPECT_EQ(500, dev_mix_get_new_write_point(dm));

  dev_mix_frames_added(dm, 0xf00, 500);
  EXPECT_EQ(500, dev_mix_get_new_write_point(dm));

  dev_mix_frames_added(dm, 0xf00, 500);
  EXPECT_EQ(500, dev_mix_get_new_write_point(dm));

  dev_mix_destroy(dm);
}

TEST_F(DevMixTestSuite, TwoDevs) {
  dev_mix *dm = dev_mix_create(1024);
  int rc;

  rc = dev_mix_add_dev(dm, 0xf00);
  EXPECT_EQ(0, rc);
  rc = dev_mix_add_dev(dm, 0xf02);
  EXPECT_EQ(0, rc);

  dev_mix_frames_added(dm, 0xf00, 500);
  EXPECT_EQ(0, dev_mix_get_new_write_point(dm));

  dev_mix_frames_added(dm, 0xf02, 750);
  EXPECT_EQ(500, dev_mix_get_new_write_point(dm));

  dev_mix_frames_added(dm, 0xf00, 500);
  EXPECT_EQ(250, dev_mix_get_new_write_point(dm));

  dev_mix_frames_added(dm, 0xf02, 750);
  EXPECT_EQ(250, dev_mix_get_new_write_point(dm));

  dev_mix_frames_added(dm, 0xf00, 500);
  EXPECT_EQ(500, dev_mix_get_new_write_point(dm));

  dev_mix_destroy(dm);
}

}  //  namespace

int main(int argc, char **argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
