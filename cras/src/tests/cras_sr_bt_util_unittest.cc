/* Copyright 2022 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <gtest/gtest.h>

extern "C" {
#include "cras_sr_bt_util.h"
}

namespace {

TEST(CrasSrBtUtilTest, TestCrasSrBtCanBeEnabled) {
  EXPECT_EQ(cras_sr_bt_can_be_enabled(), 0);
}

}  // namespace

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
