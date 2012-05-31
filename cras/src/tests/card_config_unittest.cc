// Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stdio.h>
#include <gtest/gtest.h>

extern "C" {
#include "cras_card_config.h"
#include "cras_types.h"
}

namespace {

static unsigned int cras_volume_curve_create_default_called;
static struct cras_volume_curve* cras_volume_curve_create_default_return;
static unsigned int cras_volume_curve_create_simple_step_called;
static long cras_volume_curve_create_simple_step_max_volume;
static long cras_volume_curve_create_simple_step_volume_step;
static struct cras_volume_curve* cras_volume_curve_create_simple_step_return;

static const char CONFIG_PATH[] = "/tmp";

void CreateConfigFile(const char* name, const char* config_text) {
  FILE* f;
  char card_path[128];

  snprintf(card_path, sizeof(card_path), "%s/%s", CONFIG_PATH, name);
  f = fopen(card_path, "w");
  if (f == NULL)
    return;

  fprintf(f, "%s", config_text);

  fclose(f);
}

class CardConfigTestSuite : public testing::Test{
  protected:
    virtual void SetUp() {
      cras_volume_curve_create_default_called = 0;
      cras_volume_curve_create_default_return = NULL;
      cras_volume_curve_create_simple_step_called = 0;
      cras_volume_curve_create_simple_step_return = NULL;
    }
};

// Test that no config is returned if the file doesn't exist.
TEST_F(CardConfigTestSuite, NoConfigFound) {
  struct cras_card_config* config;

  config = cras_card_config_create(CONFIG_PATH, "no_effing_way_this_exists");
  EXPECT_EQ(NULL, config);
}

// Test an empty config file, should return a config, but give back the default
// volume curve.
TEST_F(CardConfigTestSuite, EmptyConfigFileReturnsValidConfigDefaultCurves) {
  static const char empty_config_text[] = "";
  static const char empty_config_name[] = "EmptyConfigCard";
  struct cras_card_config* config;
  struct cras_volume_curve* curve;

  CreateConfigFile(empty_config_name, empty_config_text);

  config = cras_card_config_create(CONFIG_PATH, empty_config_name);
  EXPECT_NE(static_cast<struct cras_card_config*>(NULL), config);

  curve = cras_card_config_get_volume_curve_for_control(config, "asdf");
  EXPECT_EQ(1, cras_volume_curve_create_default_called);

  cras_card_config_destroy(config);
}

// Getting a curve from a null config should return a default curve.
TEST_F(CardConfigTestSuite, NullConfigGivesDefaultVolumeCurve) {
  struct cras_volume_curve* curve;

  curve = cras_card_config_get_volume_curve_for_control(NULL, "asdf");
  EXPECT_EQ(1, cras_volume_curve_create_default_called);
}

// Test getting a curve from a simple_step configuration.
TEST_F(CardConfigTestSuite, SimpleStepConfig) {
  static const char simple_config_name[] = "simple";
  static const char simple_config_text[] =
    "[Card1]\n"
    "volume_curve = simple_step\n"
    "volume_step = 75\n"
    "max_volume = -600\n";
  struct cras_card_config* config;
  struct cras_volume_curve* curve;

  CreateConfigFile(simple_config_name, simple_config_text);

  config = cras_card_config_create(CONFIG_PATH, simple_config_name);
  EXPECT_NE(static_cast<struct cras_card_config*>(NULL), config);

  // Unknown config should return default curve.
  curve = cras_card_config_get_volume_curve_for_control(NULL, "asdf");
  EXPECT_EQ(1, cras_volume_curve_create_default_called);
  cras_volume_curve_create_default_called = 0;

  // Test a config that specifies simple_step.
  curve = cras_card_config_get_volume_curve_for_control(config, "Card1");
  EXPECT_EQ(0, cras_volume_curve_create_default_called);
  EXPECT_EQ(1, cras_volume_curve_create_simple_step_called);
  EXPECT_EQ(-600, cras_volume_curve_create_simple_step_max_volume);
  EXPECT_EQ(75, cras_volume_curve_create_simple_step_volume_step);

  cras_card_config_destroy(config);
}

// Stubs.
extern "C" {

struct cras_volume_curve* cras_volume_curve_create_default() {
  cras_volume_curve_create_default_called++;
  return cras_volume_curve_create_default_return;
}

struct cras_volume_curve *cras_volume_curve_create_simple_step(
    long max_volume,
    long volume_step) {
  cras_volume_curve_create_simple_step_called++;
  cras_volume_curve_create_simple_step_max_volume = max_volume;
  cras_volume_curve_create_simple_step_volume_step = volume_step;
  return cras_volume_curve_create_simple_step_return;
}

}

}  //  namespace

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
