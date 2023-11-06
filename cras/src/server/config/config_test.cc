// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <syslog.h>

#include "cras/src/server/config/cras_board_config.h"
#include "gtest/gtest.h"

TEST(BoardConfigTest, Defaults) {
  struct cras_board_config* config = cras_board_config_create(nullptr);
  ASSERT_TRUE(config);
  EXPECT_EQ(config->hw_echo_ref_disabled, 1);
  EXPECT_EQ(config->default_output_buffer_size, 512);
  cras_board_config_destroy(config);
}

TEST(BoardConfigTest, Load) {
  struct cras_board_config* config =
      cras_board_config_create("cras/src/server/config/testdata");
  ASSERT_TRUE(config);
  EXPECT_EQ(config->hw_echo_ref_disabled, 0);
  EXPECT_EQ(config->aec_supported, 1);
  EXPECT_EQ(config->aec_group_id, 3);
  EXPECT_EQ(config->default_output_buffer_size, 256);
  EXPECT_STREQ(config->ucm_ignore_suffix, "Ignore Suffix");
  cras_board_config_destroy(config);
}

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  openlog(NULL, LOG_PERROR, LOG_USER);
  return RUN_ALL_TESTS();
}
