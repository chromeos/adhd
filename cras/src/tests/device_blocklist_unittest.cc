// Copyright 2012 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <gtest/gtest.h>
#include <linux/limits.h>
#include <stdio.h>

#include "cras/src/server/config/cras_device_blocklist.h"
#include "cras/src/tests/test_util.h"

namespace {

static const char CONFIG_FILENAME[] = "device_blocklist";

void CreateConfigFile(const char* config_text) {
  FILE* f;
  char card_path[PATH_MAX];

  snprintf(card_path, sizeof(card_path), "%s/%s", test_tmpdir(),
           CONFIG_FILENAME);
  f = fopen(card_path, "w");
  if (f == NULL) {
    return;
  }

  fprintf(f, "%s", config_text);

  fclose(f);
}

TEST(Blocklist, EmptyBlocklist) {
  static const char empty_config_text[] = "";
  struct cras_device_blocklist* blocklist;

  CreateConfigFile(empty_config_text);

  blocklist = cras_device_blocklist_create(test_tmpdir());
  ASSERT_NE(static_cast<cras_device_blocklist*>(NULL), blocklist);
  EXPECT_EQ(0, cras_device_blocklist_check(blocklist, 0x0d8c, 0x0008, 0, 0));

  cras_device_blocklist_destroy(blocklist);
}

TEST(Blocklist, BlockListOneUsbOutput) {
  static const char usb_output_config_text[] =
      "[USB_Outputs]\n"
      "0d8c_0008_00000012_0 = 1\n";
  struct cras_device_blocklist* blocklist;

  CreateConfigFile(usb_output_config_text);

  blocklist = cras_device_blocklist_create(test_tmpdir());
  ASSERT_NE(static_cast<cras_device_blocklist*>(NULL), blocklist);

  EXPECT_EQ(0, cras_device_blocklist_check(blocklist, 0x0d8d, 0x0008, 0x12, 0));
  EXPECT_EQ(0, cras_device_blocklist_check(blocklist, 0x0d8c, 0x0009, 0x12, 0));
  EXPECT_EQ(0, cras_device_blocklist_check(blocklist, 0x0d8c, 0x0008, 0x13, 0));
  EXPECT_EQ(0, cras_device_blocklist_check(blocklist, 0x0d8c, 0x0008, 0x12, 1));
  EXPECT_EQ(1, cras_device_blocklist_check(blocklist, 0x0d8c, 0x0008, 0x12, 0));

  cras_device_blocklist_destroy(blocklist);
}

TEST(Blocklist, BlockListTwoUsbOutput) {
  static const char usb_output_config_text[] =
      "[USB_Outputs]\n"
      "0d8c_0008_00000000_0 = 1\n"
      "0d8c_0009_00000000_0 = 1\n";
  struct cras_device_blocklist* blocklist;

  CreateConfigFile(usb_output_config_text);

  blocklist = cras_device_blocklist_create(test_tmpdir());
  ASSERT_NE(static_cast<cras_device_blocklist*>(NULL), blocklist);

  EXPECT_EQ(1, cras_device_blocklist_check(blocklist, 0x0d8c, 0x0009, 0, 0));
  EXPECT_EQ(1, cras_device_blocklist_check(blocklist, 0x0d8c, 0x0008, 0, 0));
  EXPECT_EQ(0, cras_device_blocklist_check(blocklist, 0x0d8c, 0x0008, 0, 1));

  cras_device_blocklist_destroy(blocklist);
}

}  //  namespace
