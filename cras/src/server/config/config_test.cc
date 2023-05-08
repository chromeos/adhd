// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <syslog.h>

#include "cras/src/server/config/cras_board_config.h"
#include "gtest/gtest.h"

TEST(BoardConfigTest, Defaults) {
  struct cras_board_config config = {};
  cras_board_config_get(nullptr, &config);
  EXPECT_EQ(config.hw_echo_ref_disabled, 1);
  EXPECT_EQ(config.default_output_buffer_size, 512);
  cras_board_config_clear(&config);
}

TEST(BoardConfigTest, Load) {
  struct cras_board_config config = {};
  cras_board_config_get("cras/src/server/config/testdata", &config);
  EXPECT_EQ(config.hw_echo_ref_disabled, 0);
  EXPECT_EQ(config.aec_supported, 1);
  EXPECT_EQ(config.aec_group_id, 3);
  EXPECT_EQ(config.default_output_buffer_size, 256);
  cras_board_config_clear(&config);
}

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  openlog(NULL, LOG_PERROR, LOG_USER);
  return RUN_ALL_TESTS();
}
