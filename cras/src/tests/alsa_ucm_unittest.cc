// Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <gtest/gtest.h>
#include <iniparser.h>
#include <stdio.h>
#include <map>

extern "C" {
#include "cras_alsa_ucm.h"
#include "cras_types.h"

//  Include C file to test static functions.
#include "cras_alsa_ucm.c"
}

namespace {

static int snd_use_case_mgr_open_return;
static snd_use_case_mgr_t *snd_use_case_mgr_open_mgr_ptr;
static unsigned snd_use_case_mgr_open_called;
static unsigned snd_use_case_mgr_close_called;
static unsigned snd_use_case_get_called;
static std::vector<std::string> snd_use_case_get_id;
static std::map<std::string, int> snd_use_case_get_ret_value;
static int snd_use_case_set_return;
static std::map<std::string, std::string> snd_use_case_get_value;
static unsigned snd_use_case_set_called;
static const char **fake_list;
static unsigned fake_list_size;
static unsigned snd_use_case_free_list_called;

static void ResetStubData() {
  snd_use_case_mgr_open_called = 0;
  snd_use_case_mgr_open_return = 0;
  snd_use_case_mgr_close_called = 0;
  snd_use_case_set_return = 0;
  snd_use_case_get_called = 0;
  snd_use_case_set_called = 0;
  snd_use_case_free_list_called = 0;
  snd_use_case_get_id.clear();
  snd_use_case_get_value.clear();
  snd_use_case_get_ret_value.clear();
}

TEST(AlsaUcm, CreateFailInvalidCard) {
  ResetStubData();
  EXPECT_EQ(NULL, ucm_create(NULL));
  EXPECT_EQ(0, snd_use_case_mgr_open_called);
}

TEST(AlsaUcm, CreateFailCardNotFound) {
  ResetStubData();
  snd_use_case_mgr_open_return = -1;
  EXPECT_EQ(NULL, ucm_create("foo"));
  EXPECT_EQ(1, snd_use_case_mgr_open_called);
}

TEST(AlsaUcm, CreateFailNoHiFi) {
  ResetStubData();
  snd_use_case_set_return = -1;
  EXPECT_EQ(NULL, ucm_create("foo"));
  EXPECT_EQ(1, snd_use_case_mgr_open_called);
  EXPECT_EQ(1, snd_use_case_set_called);
  EXPECT_EQ(1, snd_use_case_mgr_close_called);
}

TEST(AlsaUcm, CreateSuccess) {
  snd_use_case_mgr_t* mgr;

  ResetStubData();
  snd_use_case_mgr_open_mgr_ptr = reinterpret_cast<snd_use_case_mgr_t*>(0x55);

  mgr = ucm_create("foo");
  EXPECT_NE(static_cast<snd_use_case_mgr_t*>(NULL), mgr);
  EXPECT_EQ(1, snd_use_case_mgr_open_called);
  EXPECT_EQ(1, snd_use_case_set_called);
  EXPECT_EQ(0, snd_use_case_mgr_close_called);

  ucm_destroy(mgr);
  EXPECT_EQ(1, snd_use_case_mgr_close_called);
}

TEST(AlsaUcm, CheckEnabledEmptyList) {
  snd_use_case_mgr_t* mgr = reinterpret_cast<snd_use_case_mgr_t*>(0x55);
  fake_list = NULL;
  fake_list_size = 0;

  ResetStubData();

  EXPECT_EQ(0, ucm_set_enabled(mgr, "Dev1", 0));
  EXPECT_EQ(0, snd_use_case_set_called);

  EXPECT_EQ(0, ucm_set_enabled(mgr, "Dev1", 1));
  EXPECT_EQ(1, snd_use_case_set_called);

  EXPECT_EQ(0, snd_use_case_free_list_called);
}

TEST(AlsaUcm, CheckEnabledAlready) {
  snd_use_case_mgr_t* mgr = reinterpret_cast<snd_use_case_mgr_t*>(0x55);
  const char *enabled[] = { "Dev2", "Dev1" };
  fake_list = enabled;
  fake_list_size = 2;

  ResetStubData();

  EXPECT_EQ(0, ucm_set_enabled(mgr, "Dev1", 1));
  EXPECT_EQ(0, snd_use_case_set_called);

  EXPECT_EQ(0, ucm_set_enabled(mgr, "Dev1", 0));
  EXPECT_EQ(1, snd_use_case_set_called);

  EXPECT_EQ(2, snd_use_case_free_list_called);
}

TEST(AlsaUcm, GetEdidForDev) {
  snd_use_case_mgr_t* mgr = reinterpret_cast<snd_use_case_mgr_t*>(0x55);
  std::string id = "=EDIDFile/Dev1/HiFi";
  std::string value = "EdidFileName";
  const char *file_name;

  ResetStubData();

  snd_use_case_get_value[id] = value;
  snd_use_case_get_ret_value[id] = 0;

  file_name = ucm_get_edid_file_for_dev(mgr, "Dev1");
  ASSERT_TRUE(file_name);
  EXPECT_EQ(0, strcmp(file_name, value.c_str()));
  free((void*)file_name);

  ASSERT_EQ(1, snd_use_case_get_called);
  EXPECT_EQ(snd_use_case_get_id[0], id);
}

TEST(AlsaUcm, GetCapControlForDev) {
  snd_use_case_mgr_t* mgr = reinterpret_cast<snd_use_case_mgr_t*>(0x55);
  char *cap_control;
  std::string id = "=CaptureControl/Dev1/HiFi";
  std::string value = "MIC";

  ResetStubData();

  snd_use_case_get_value[id] = value;
  snd_use_case_get_ret_value[id] = 0;

  cap_control = ucm_get_cap_control(mgr, "Dev1");
  ASSERT_TRUE(cap_control);
  EXPECT_EQ(0, strcmp(cap_control, value.c_str()));
  free(cap_control);

  ASSERT_EQ(1, snd_use_case_get_called);
  EXPECT_EQ(snd_use_case_get_id[0], id);
}

TEST(AlsaUcm, GetOverrideType) {
  snd_use_case_mgr_t* mgr = reinterpret_cast<snd_use_case_mgr_t*>(0x55);
  const char *override_type_name;
  std::string id = "=OverrideNodeType/Dev1/HiFi";
  std::string value = "HDMI";

  ResetStubData();

  snd_use_case_get_value[id] = value;
  snd_use_case_get_ret_value[id] = 0;

  override_type_name = ucm_get_override_type_name(mgr, "Dev1");
  ASSERT_TRUE(override_type_name);
  EXPECT_EQ(0, strcmp(override_type_name, value.c_str()));
  free((void*)override_type_name);

  ASSERT_EQ(1, snd_use_case_get_called);
  EXPECT_EQ(snd_use_case_get_id[0], id);
}

TEST(AlsaUcm, GetSectionForVar) {
  snd_use_case_mgr_t* mgr = reinterpret_cast<snd_use_case_mgr_t*>(0x55);
  const char *section_name;

  ResetStubData();

  const char *sections[] = { "Sec1", "Comment for Sec1", "Sec2",
                             "Comment for Sec2" };
  fake_list = sections;
  fake_list_size = 4;
  std::string id_1 = "=Var/Sec1/HiFi";
  std::string id_2 = "=Var/Sec2/HiFi";
  std::string value_1 = "Value1";
  std::string value_2 = "Value2";

  snd_use_case_get_ret_value[id_1] = 0;
  snd_use_case_get_value[id_1] = value_1;
  snd_use_case_get_ret_value[id_2] = 0;
  snd_use_case_get_value[id_2] = value_2;

  section_name = ucm_get_section_for_var(mgr, "Var", "Value2", "Identifier");

  ASSERT_TRUE(section_name);
  EXPECT_EQ(0, strcmp(section_name, "Sec2"));
  free((void*)section_name);

  ASSERT_EQ(2, snd_use_case_get_called);
  EXPECT_EQ(snd_use_case_get_id[0], id_1);
  EXPECT_EQ(snd_use_case_get_id[1], id_2);
}

TEST(AlsaUcm, GetDevForJack) {
  snd_use_case_mgr_t* mgr = reinterpret_cast<snd_use_case_mgr_t*>(0x55);
  const char *dev_name;

  ResetStubData();

  const char *devices[] = { "Dev1", "Comment for Dev1", "Dev2",
                            "Comment for Dev2" };
  fake_list = devices;
  fake_list_size = 4;
  std::string id_1 = "=JackName/Dev1/HiFi";
  std::string id_2 = "=JackName/Dev2/HiFi";
  std::string value_1 = "Value1";
  std::string value_2 = "Value2";

  snd_use_case_get_ret_value[id_1] = 0;
  snd_use_case_get_value[id_1] = value_1;
  snd_use_case_get_ret_value[id_2] = 0;
  snd_use_case_get_value[id_2] = value_2;
  dev_name = ucm_get_dev_for_jack(mgr, value_2.c_str());
  ASSERT_TRUE(dev_name);
  EXPECT_EQ(0, strcmp(dev_name, "Dev2"));
  free((void*)dev_name);

  ASSERT_EQ(2, snd_use_case_get_called);
  EXPECT_EQ(snd_use_case_get_id[0], id_1);
  EXPECT_EQ(snd_use_case_get_id[1], id_2);
}

TEST(AlsaFlag, GetFlag) {
  snd_use_case_mgr_t* mgr = reinterpret_cast<snd_use_case_mgr_t*>(0x55);
  char *flag_value;

  std::string id = "=FlagName//HiFi";
  std::string value = "1";
  ResetStubData();

  snd_use_case_get_value[id] = value;

  flag_value = ucm_get_flag(mgr, "FlagName");
  ASSERT_TRUE(flag_value);
  EXPECT_EQ(0, strcmp(flag_value, value.c_str()));
  free(flag_value);

  ASSERT_EQ(1, snd_use_case_get_called);
  EXPECT_EQ(snd_use_case_get_id[0], id);
}

/* Stubs */

extern "C" {

int snd_use_case_mgr_open(snd_use_case_mgr_t** uc_mgr, const char* card_name) {
  snd_use_case_mgr_open_called++;
  *uc_mgr = snd_use_case_mgr_open_mgr_ptr;
  return snd_use_case_mgr_open_return;
}

int snd_use_case_mgr_close(snd_use_case_mgr_t *uc_mgr) {
  snd_use_case_mgr_close_called++;
  return 0;
}

int snd_use_case_get(snd_use_case_mgr_t* uc_mgr,
                     const char *identifier,
                     const char **value) {
  snd_use_case_get_called++;
  *value = strdup(snd_use_case_get_value[identifier].c_str());
  snd_use_case_get_id.push_back(std::string(identifier));
  return snd_use_case_get_ret_value[identifier];
}

int snd_use_case_set(snd_use_case_mgr_t* uc_mgr,
                     const char *identifier,
                     const char *value) {
  snd_use_case_set_called++;
  return snd_use_case_set_return;;
}

int snd_use_case_get_list(snd_use_case_mgr_t *uc_mgr,
                          const char *identifier,
                          const char **list[]) {
  *list = fake_list;
  return fake_list_size;
}

int snd_use_case_free_list(const char *list[], int items) {
  snd_use_case_free_list_called++;
  return 0;
}

} /* extern "C" */

}  //  namespace

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
