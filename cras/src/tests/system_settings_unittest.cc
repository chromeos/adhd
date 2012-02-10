// Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stdio.h>
#include <gtest/gtest.h>

extern "C" {
#include "cras_system_settings.h"
#include "cras_types.h"
}

namespace {
size_t volume_changed_called;
size_t volume_changed_value;

static void volume_changed(size_t volume) {
  volume_changed_called++;
  volume_changed_value = volume;
}

TEST(SystemSettingsSuite, DefaultVolume) {
  cras_system_settings_init();
  EXPECT_EQ(100, cras_system_get_volume());
}

TEST(SystemSettingsSuite, SetVolume) {
  cras_system_settings_init();
  cras_system_set_volume(0);
  EXPECT_EQ(0, cras_system_get_volume());
  cras_system_set_volume(50);
  EXPECT_EQ(50, cras_system_get_volume());
  cras_system_set_volume(CRAS_MAX_SYSTEM_VOLUME);
  EXPECT_EQ(CRAS_MAX_SYSTEM_VOLUME, cras_system_get_volume());
  cras_system_set_volume(CRAS_MAX_SYSTEM_VOLUME + 1);
  EXPECT_EQ(CRAS_MAX_SYSTEM_VOLUME, cras_system_get_volume());
}

TEST(SystemSettingsSuite, ChangedCallback) {
  cras_system_settings_init();
  cras_system_register_volume_changed_cb(volume_changed);
  volume_changed_called = 0;
  cras_system_set_volume(55);
  EXPECT_EQ(55, cras_system_get_volume());
  EXPECT_EQ(1, volume_changed_called);
  EXPECT_EQ(55, volume_changed_value);

  cras_system_register_volume_changed_cb(NULL);
  volume_changed_called = 0;
  cras_system_set_volume(44);
  EXPECT_EQ(44, cras_system_get_volume());
  EXPECT_EQ(0, volume_changed_called);
}

}  //  namespace

int main(int argc, char **argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
