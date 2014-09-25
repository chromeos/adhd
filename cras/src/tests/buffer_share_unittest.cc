// Copyright (c) 2014 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stdio.h>
#include <gtest/gtest.h>

extern "C" {
#include "cras_types.h"
#include "buffer_share.h"
}

namespace {

class BufferShareTestSuite : public testing::Test{
  protected:
    virtual void SetUp() {
    }

    virtual void TearDown() {
    }

};

TEST_F(BufferShareTestSuite, Create) {
  buffer_share *dm = buffer_share_create(1024);
  EXPECT_NE(static_cast<buffer_share *>(NULL), dm);
  buffer_share_destroy(dm);
}

TEST_F(BufferShareTestSuite, AddRmDev) {
  buffer_share *dm = buffer_share_create(1024);
  int rc;

  rc = buffer_share_add_dev(dm, 0xf00);
  EXPECT_EQ(0, rc);
  rc = buffer_share_add_dev(dm, 0xf00);
  EXPECT_NE(0, rc);

  rc = buffer_share_rm_dev(dm, 0xf00);
  EXPECT_EQ(0, rc);
  rc = buffer_share_rm_dev(dm, 0xf00);
  EXPECT_NE(0, rc);

  buffer_share_destroy(dm);
}

TEST_F(BufferShareTestSuite, AddManyDevs) {
  buffer_share *dm = buffer_share_create(1024);
  int rc;

  for (unsigned int i = 0; i < INITIAL_DEV_SIZE; i++) {
    rc = buffer_share_add_dev(dm, 0xf00 + i);
    EXPECT_EQ(0, rc);
  }

  rc = buffer_share_add_dev(dm, 0xf00 + INITIAL_DEV_SIZE);
  EXPECT_EQ(0, rc);

  buffer_share_destroy(dm);
}

TEST_F(BufferShareTestSuite, OneDev) {
  buffer_share *dm = buffer_share_create(1024);
  int rc;

  rc = buffer_share_add_dev(dm, 0xf00);
  EXPECT_EQ(0, rc);

  buffer_share_offset_update(dm, 0xf00, 500);
  EXPECT_EQ(500, buffer_share_get_new_write_point(dm));

  buffer_share_offset_update(dm, 0xf00, 500);
  EXPECT_EQ(500, buffer_share_get_new_write_point(dm));

  buffer_share_offset_update(dm, 0xf00, 500);
  EXPECT_EQ(500, buffer_share_get_new_write_point(dm));

  buffer_share_offset_update(dm, 0xf00, 500);
  EXPECT_EQ(500, buffer_share_get_new_write_point(dm));

  buffer_share_destroy(dm);
}

TEST_F(BufferShareTestSuite, TwoDevs) {
  buffer_share *dm = buffer_share_create(1024);
  int rc;

  rc = buffer_share_add_dev(dm, 0xf00);
  EXPECT_EQ(0, rc);
  rc = buffer_share_add_dev(dm, 0xf02);
  EXPECT_EQ(0, rc);

  buffer_share_offset_update(dm, 0xf00, 500);
  EXPECT_EQ(0, buffer_share_get_new_write_point(dm));

  buffer_share_offset_update(dm, 0xf02, 750);
  EXPECT_EQ(500, buffer_share_get_new_write_point(dm));

  buffer_share_offset_update(dm, 0xf00, 500);
  EXPECT_EQ(250, buffer_share_get_new_write_point(dm));

  buffer_share_offset_update(dm, 0xf02, 750);
  EXPECT_EQ(250, buffer_share_get_new_write_point(dm));

  buffer_share_offset_update(dm, 0xf00, 500);
  EXPECT_EQ(500, buffer_share_get_new_write_point(dm));

  buffer_share_destroy(dm);
}

}  //  namespace

int main(int argc, char **argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
