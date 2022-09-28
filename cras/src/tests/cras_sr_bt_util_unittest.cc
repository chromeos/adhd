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
  EXPECT_NE(cras_sr_bt_can_be_enabled(), CRAS_SR_BT_CAN_BE_ENABLED_STATUS_OK);
}

}  // namespace

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
