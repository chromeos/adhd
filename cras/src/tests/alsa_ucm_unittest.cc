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
#include "utlist.h"

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
static std::vector<std::pair<std::string, std::string> > snd_use_case_set_param;
static std::map<std::string, const char **> fake_list;
static std::map<std::string, unsigned> fake_list_size;
static unsigned snd_use_case_free_list_called;

static void ResetStubData() {
  snd_use_case_mgr_open_called = 0;
  snd_use_case_mgr_open_return = 0;
  snd_use_case_mgr_close_called = 0;
  snd_use_case_set_return = 0;
  snd_use_case_get_called = 0;
  snd_use_case_set_called = 0;
  snd_use_case_set_param.clear();
  snd_use_case_free_list_called = 0;
  snd_use_case_get_id.clear();
  snd_use_case_get_value.clear();
  snd_use_case_get_ret_value.clear();
  fake_list.clear();
  fake_list_size.clear();
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

  ResetStubData();
  fake_list["_enadevs"] = NULL;
  fake_list_size["_enadevs"] = 0;

  EXPECT_EQ(0, ucm_set_enabled(mgr, "Dev1", 0));
  EXPECT_EQ(0, snd_use_case_set_called);

  EXPECT_EQ(0, ucm_set_enabled(mgr, "Dev1", 1));
  EXPECT_EQ(1, snd_use_case_set_called);

  EXPECT_EQ(0, snd_use_case_free_list_called);
}

TEST(AlsaUcm, CheckEnabledAlready) {
  snd_use_case_mgr_t* mgr = reinterpret_cast<snd_use_case_mgr_t*>(0x55);
  const char *enabled[] = { "Dev2", "Dev1" };

  ResetStubData();

  fake_list["_enadevs"] = enabled;
  fake_list_size["_enadevs"] = 2;

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
  fake_list["Identifier"] = sections;
  fake_list_size["Identifier"] = 4;
  std::string id_1 = "=Var/Sec1/HiFi";
  std::string id_2 = "=Var/Sec2/HiFi";
  std::string value_1 = "Value1";
  std::string value_2 = "Value2";

  snd_use_case_get_ret_value[id_1] = 0;
  snd_use_case_get_value[id_1] = value_1;
  snd_use_case_get_ret_value[id_2] = 0;
  snd_use_case_get_value[id_2] = value_2;

  section_name = ucm_get_section_for_var(mgr, "Var", "Value2", "Identifier",
                                         CRAS_STREAM_OUTPUT);

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
  const char *devices[] = { "Dev1", "Comment for Dev1", "Dev2",
                            "Comment for Dev2" };

  ResetStubData();

  fake_list["_devices/HiFi"] = devices;
  fake_list_size["_devices/HiFi"] = 4;
  std::string id_1 = "=JackName/Dev1/HiFi";
  std::string id_2 = "=JackName/Dev2/HiFi";
  std::string value_1 = "Value1";
  std::string value_2 = "Value2";

  snd_use_case_get_ret_value[id_1] = 0;
  snd_use_case_get_value[id_1] = value_1;
  snd_use_case_get_ret_value[id_2] = 0;
  snd_use_case_get_value[id_2] = value_2;
  dev_name = ucm_get_dev_for_jack(mgr, value_2.c_str(), CRAS_STREAM_OUTPUT);
  ASSERT_TRUE(dev_name);
  EXPECT_EQ(0, strcmp(dev_name, "Dev2"));
  free((void*)dev_name);

  ASSERT_EQ(2, snd_use_case_get_called);
  EXPECT_EQ(snd_use_case_get_id[0], id_1);
  EXPECT_EQ(snd_use_case_get_id[1], id_2);
}

TEST(AlsaUcm, GetDeviceNameForDevice) {
  snd_use_case_mgr_t* mgr = reinterpret_cast<snd_use_case_mgr_t*>(0x55);
  const char *input_dev_name, *output_dev_name;
  const char *devices[] = { "Dev1", "Comment for Dev1", "Dev2",
                            "Comment for Dev2" };

  ResetStubData();

  fake_list["_devices/HiFi"] = devices;
  fake_list_size["_devices/HiFi"] = 4;
  std::string id_1 = "=CapturePCM/Dev1/HiFi";
  std::string id_2 = "=PlaybackPCM/Dev2/HiFi";
  std::string value_1 = "DeviceName1";
  std::string value_2 = "DeviceName2";

  snd_use_case_get_ret_value[id_1] = 0;
  snd_use_case_get_value[id_1] = value_1;
  snd_use_case_get_ret_value[id_2] = 0;
  snd_use_case_get_value[id_2] = value_2;
  input_dev_name = ucm_get_device_name_for_dev(mgr, "Dev1", CRAS_STREAM_INPUT);
  output_dev_name = ucm_get_device_name_for_dev(mgr, "Dev2", CRAS_STREAM_OUTPUT);
  ASSERT_TRUE(input_dev_name);
  ASSERT_TRUE(output_dev_name);
  EXPECT_EQ(0, strcmp(input_dev_name, value_1.c_str()));
  EXPECT_EQ(0, strcmp(output_dev_name, value_2.c_str()));

  ASSERT_EQ(2, snd_use_case_get_called);
  EXPECT_EQ(snd_use_case_get_id[0], id_1);
  EXPECT_EQ(snd_use_case_get_id[1], id_2);
}

TEST(AlsaUcm, GetHotwordModels) {
  snd_use_case_mgr_t* mgr = reinterpret_cast<snd_use_case_mgr_t*>(0x55);
  const char *models;
  const char *modifiers[] = { "Mod1",
                            "Comment1",
                            "Hotword Model en",
                            "Comment2",
                            "Hotword Model jp",
                            "Comment3",
                            "Mod2",
                            "Comment4",
                            "Hotword Model de",
                            "Comment5" };
  ResetStubData();

  fake_list["_modifiers/HiFi"] = modifiers;
  fake_list_size["_modifiers/HiFi"] = 10;

  models = ucm_get_hotword_models(mgr);
  ASSERT_TRUE(models);
  EXPECT_EQ(0, strcmp(models, "en,jp,de"));
}

TEST(AlsaUcm, SetHotwordModel) {
  snd_use_case_mgr_t* mgr = reinterpret_cast<snd_use_case_mgr_t*>(0x55);
  const char *modifiers[] = { "Hotword Model en",
                              "Comment1",
                              "Hotword Model jp",
                              "Comment2",
                              "Hotword Model de",
                              "Comment3" };
  const char *enabled_mods[] = { "Hotword Model en" };
  ResetStubData();

  fake_list["_modifiers/HiFi"] = modifiers;
  fake_list_size["_modifiers/HiFi"] = 6;

  EXPECT_EQ(-EINVAL, ucm_set_hotword_model(mgr, "zh"));
  EXPECT_EQ(0, snd_use_case_set_called);

  fake_list["_enamods"] = enabled_mods;
  fake_list_size["_enamods"] = 1;
  ucm_set_hotword_model(mgr, "jp");

  EXPECT_EQ(2, snd_use_case_set_called);
  EXPECT_EQ(snd_use_case_set_param[0],
      std::make_pair(std::string("_dismod"), std::string("Hotword Model en")));
  EXPECT_EQ(snd_use_case_set_param[1],
      std::make_pair(std::string("_enamod"), std::string("Hotword Model jp")));
}

TEST(AlsaUcm, SwapModeExists) {
  snd_use_case_mgr_t* mgr = reinterpret_cast<snd_use_case_mgr_t*>(0x55);
  int rc;
  const char *modifiers_1[] = { "Speaker Swap Mode",
                                "Comment for Speaker Swap Mode",
                                "Microphone Swap Mode",
                                "Comment for Microphone Swap Mode" };
  const char *modifiers_2[] = { "Speaker Some Mode",
                                "Comment for Speaker Some Mode",
                                "Microphone Some Mode",
                                "Comment for Microphone Some Mode" };

  ResetStubData();

  fake_list["_modifiers/HiFi"] = modifiers_1;
  fake_list_size["_modifiers/HiFi"] = 4;
  rc = ucm_swap_mode_exists(mgr);
  EXPECT_EQ(1, rc);

  fake_list["_modifiers/HiFi"] = modifiers_2;
  fake_list_size["_modifiers/HiFi"] = 4;
  rc = ucm_swap_mode_exists(mgr);
  EXPECT_EQ(0, rc);
}

TEST(AlsaUcm, EnableSwapMode) {
  snd_use_case_mgr_t* mgr = reinterpret_cast<snd_use_case_mgr_t*>(0x55);
  int rc;
  const char *modifiers[] = { "Speaker Swap Mode",
                              "Comment for Speaker Swap Mode",
                              "Microphone Swap Mode",
                              "Comment for Microphone Swap Mode" };
  const char *modifiers_enabled[] = {"Speaker Swap Mode"};

  ResetStubData();

  fake_list["_modifiers/HiFi"] = modifiers;
  fake_list_size["_modifiers/HiFi"] = 4;

  fake_list["_enamods"] = modifiers_enabled;
  fake_list_size["_enamods"] = 1;

  snd_use_case_set_return = 0;

  rc = ucm_enable_swap_mode(mgr, "Headphone", 1);
  EXPECT_EQ(-EPERM, rc);
  EXPECT_EQ(0, snd_use_case_set_called);

  rc = ucm_enable_swap_mode(mgr, "Speaker", 1);
  EXPECT_EQ(0, rc);
  EXPECT_EQ(0, snd_use_case_set_called);

  rc = ucm_enable_swap_mode(mgr, "Microphone", 1);
  EXPECT_EQ(0, rc);
  EXPECT_EQ(1, snd_use_case_set_called);
}

TEST(AlsaUcm, DisableSwapMode) {
  snd_use_case_mgr_t* mgr = reinterpret_cast<snd_use_case_mgr_t*>(0x55);
  int rc;
  const char *modifiers[] = { "Speaker Swap Mode",
                              "Comment for Speaker Swap Mode",
                              "Microphone Swap Mode",
                              "Comment for Microphone Swap Mode" };
  const char *modifiers_enabled[] = {"Speaker Swap Mode"};

  ResetStubData();

  fake_list["_modifiers/HiFi"] = modifiers;
  fake_list_size["_modifiers/HiFi"] = 4;

  fake_list["_enamods"] = modifiers_enabled;
  fake_list_size["_enamods"] = 1;

  snd_use_case_set_return = 0;

  rc = ucm_enable_swap_mode(mgr, "Headphone", 0);
  EXPECT_EQ(-EPERM, rc);
  EXPECT_EQ(0, snd_use_case_set_called);

  rc = ucm_enable_swap_mode(mgr, "Microphone", 0);
  EXPECT_EQ(0, rc);
  EXPECT_EQ(0, snd_use_case_set_called);

  rc = ucm_enable_swap_mode(mgr, "Speaker", 0);
  EXPECT_EQ(0, rc);
  EXPECT_EQ(1, snd_use_case_set_called);

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

TEST(AlsaUcm, ModifierEnabled) {
  snd_use_case_mgr_t* mgr = reinterpret_cast<snd_use_case_mgr_t*>(0x55);
  int enabled;

  ResetStubData();

  const char *mods[] = { "Mod1", "Mod2" };
  fake_list["_enamods"] = mods;
  fake_list_size["_enamods"] = 2;

  enabled = modifier_enabled(mgr, "Mod1");
  EXPECT_EQ(1, enabled);
  enabled = modifier_enabled(mgr, "Mod2");
  EXPECT_EQ(1, enabled);
  enabled = modifier_enabled(mgr, "Mod3");
  EXPECT_EQ(0, enabled);
}

TEST(AlsaUcm, SetModifierEnabled) {
  snd_use_case_mgr_t* mgr = reinterpret_cast<snd_use_case_mgr_t*>(0x55);

  ResetStubData();

  ucm_set_modifier_enabled(mgr, "Mod1", 1);
  EXPECT_EQ(snd_use_case_set_param[0],
            std::make_pair(std::string("_enamod"), std::string("Mod1")));
  EXPECT_EQ(1, snd_use_case_set_called);
  ucm_set_modifier_enabled(mgr, "Mod1", 0);
  EXPECT_EQ(snd_use_case_set_param[1],
            std::make_pair(std::string("_dismod"), std::string("Mod1")));
  EXPECT_EQ(2, snd_use_case_set_called);
}

TEST(AlsaUcm, EndWithSuffix) {
  EXPECT_EQ(1, ucm_str_ends_with_suffix("Foo bar", "bar"));
  EXPECT_EQ(1, ucm_str_ends_with_suffix("bar", "bar"));
  EXPECT_EQ(0, ucm_str_ends_with_suffix("Foo car", "bar"));
}

TEST(AlsaUcm, SectionExistsWithName) {
  snd_use_case_mgr_t* mgr = reinterpret_cast<snd_use_case_mgr_t*>(0x55);
  const char *sections[] = { "Sec1", "Comment for Sec1", "Sec2",
                             "Comment for Sec2" };

  ResetStubData();

  fake_list["Identifier"] = sections;
  fake_list_size["Identifier"] = 4;
  EXPECT_EQ(1, ucm_section_exists_with_name(mgr, "Sec1", "Identifier"));
  EXPECT_EQ(1, ucm_section_exists_with_name(mgr, "Sec2", "Identifier"));
  EXPECT_EQ(0, ucm_section_exists_with_name(mgr, "Sec3", "Identifier"));
}

TEST(AlsaUcm, SectionExistsWithSuffix) {
  snd_use_case_mgr_t* mgr = reinterpret_cast<snd_use_case_mgr_t*>(0x55);

  ResetStubData();

  const char *sections[] = { "Sec1 Suffix1", "Comment for Sec1",
                             "Sec2 Suffix2", "Comment for Sec2" };
  fake_list["Identifier"] = sections;
  fake_list_size["Identifier"] = 4;
  EXPECT_EQ(1, ucm_section_exists_with_suffix(mgr, "Suffix1", "Identifier"));
  EXPECT_EQ(1, ucm_section_exists_with_suffix(mgr, "Suffix2", "Identifier"));
  EXPECT_EQ(0, ucm_section_exists_with_suffix(mgr, "Suffix3", "Identifier"));
}

TEST(AlsaUcm, DisableSoftwareVolume) {
  snd_use_case_mgr_t* mgr = reinterpret_cast<snd_use_case_mgr_t*>(0x55);
  unsigned int disable_software_volume;
  std::string id = "=DisableSoftwareVolume//HiFi";
  std::string value = "1";

  ResetStubData();

  snd_use_case_get_value[id] = value;
  snd_use_case_get_ret_value[id] = 0;

  disable_software_volume = ucm_get_disable_software_volume(mgr);
  ASSERT_TRUE(disable_software_volume);

  ASSERT_EQ(1, snd_use_case_get_called);
  EXPECT_EQ(snd_use_case_get_id[0], id);
}

TEST(AlsaUcm, GetCoupledMixersForDevice) {
  snd_use_case_mgr_t* mgr = reinterpret_cast<snd_use_case_mgr_t*>(0x55);
  struct mixer_name *mixer_names_1, *mixer_names_2, *c;
  const char *devices[] = { "Dev1", "Comment for Dev1", "Dev2",
                            "Comment for Dev2" };

  ResetStubData();

  fake_list["_devices/HiFi"] = devices;
  fake_list_size["_devices/HiFi"] = 4;
  std::string id_1 = "=CoupledMixers/Dev1/HiFi";
  std::string value_1 = "Mixer Name1,Mixer Name2,Mixer Name3";
  std::string id_2 = "=CoupledMixers/Dev2/HiFi";
  std::string value_2 = "";
  snd_use_case_get_ret_value[id_1] = 0;
  snd_use_case_get_value[id_1] = value_1;
  snd_use_case_get_ret_value[id_2] = 1;
  snd_use_case_get_value[id_2] = value_2;
  mixer_names_1 = ucm_get_coupled_mixer_names(mgr, "Dev1");
  mixer_names_2 = ucm_get_coupled_mixer_names(mgr, "Dev2");

  ASSERT_TRUE(mixer_names_1);
  EXPECT_EQ(0, strcmp(mixer_names_1->name, "Mixer Name1"));
  EXPECT_EQ(0, strcmp(mixer_names_1->next->name, "Mixer Name2"));
  EXPECT_EQ(0, strcmp(mixer_names_1->next->next->name, "Mixer Name3"));
  EXPECT_EQ(NULL, mixer_names_1->next->next->next);

  EXPECT_EQ(NULL, mixer_names_2);

  DL_FOREACH(mixer_names_1, c) {
    DL_DELETE(mixer_names_1, c);
    free((void*)c->name);
    free(c);
  }
}

TEST(AlsaUcm, FreeMixerNames) {
  snd_use_case_mgr_t* mgr = reinterpret_cast<snd_use_case_mgr_t*>(0x55);
  struct mixer_name *mixer_names_1;
  const char *devices[] = { "Dev1", "Comment for Dev1"};

  ResetStubData();

  fake_list["_devices/HiFi"] = devices;
  fake_list_size["_devices/HiFi"] = 2;
  std::string id_1 = "=CoupledMixers/Dev1/HiFi";
  std::string value_1 = "Mixer Name1,Mixer Name2,Mixer Name3";
  snd_use_case_get_ret_value[id_1] = 0;
  snd_use_case_get_value[id_1] = value_1;
  mixer_names_1 = ucm_get_coupled_mixer_names(mgr, "Dev1");


  ASSERT_TRUE(mixer_names_1);
  EXPECT_EQ(0, strcmp(mixer_names_1->name, "Mixer Name1"));
  EXPECT_EQ(0, strcmp(mixer_names_1->next->name, "Mixer Name2"));
  EXPECT_EQ(0, strcmp(mixer_names_1->next->next->name, "Mixer Name3"));
  EXPECT_EQ(NULL, mixer_names_1->next->next->next);

  /* No way to actually check if memory is freed. */
  ucm_free_mixer_names(mixer_names_1);
}

TEST(AlsaUcm, MaxSoftwareGain) {
  snd_use_case_mgr_t* mgr = reinterpret_cast<snd_use_case_mgr_t*>(0x55);
  long max_software_gain;
  int ret;
  std::string id = "=MaxSoftwareGain/Internal Mic/HiFi";
  std::string value = "2000";

  ResetStubData();

  /* Value can be found in UCM. */
  snd_use_case_get_value[id] = value;
  snd_use_case_get_ret_value[id] = 0;

  ret = ucm_get_max_software_gain(mgr, "Internal Mic", &max_software_gain);

  EXPECT_EQ(0, ret);
  EXPECT_EQ(2000, max_software_gain);

  ResetStubData();

  /* Value can not be found in UCM. */
  snd_use_case_get_value[id] = "";
  snd_use_case_get_ret_value[id] = -1;

  ret = ucm_get_max_software_gain(mgr, "Internal Mic", &max_software_gain);

  ASSERT_TRUE(ret);
}

TEST(AlsaUCM, UseFullySpecifiedUCMConfig) {
  snd_use_case_mgr_t* mgr = reinterpret_cast<snd_use_case_mgr_t*>(0x55);
  int fully_specified_flag;

  std::string id = "=FullySpecifiedUCM//HiFi";
  ResetStubData();

  /* Flag is not set */
  snd_use_case_get_ret_value[id] = -1;
  fully_specified_flag = ucm_has_fully_specified_ucm_flag(mgr);
  ASSERT_FALSE(fully_specified_flag);

  /* Flag is set to "1". */
  snd_use_case_get_value[id] = std::string("1");
  snd_use_case_get_ret_value[id] = 0;
  fully_specified_flag = ucm_has_fully_specified_ucm_flag(mgr);
  ASSERT_TRUE(fully_specified_flag);

  /* Flag is set to "0". */
  snd_use_case_get_value[id] = std::string("0");
  snd_use_case_get_ret_value[id] = 0;
  fully_specified_flag = ucm_has_fully_specified_ucm_flag(mgr);
  ASSERT_FALSE(fully_specified_flag);
}

TEST(AlsaUcm, GetMixerNameForDevice) {
  snd_use_case_mgr_t* mgr = reinterpret_cast<snd_use_case_mgr_t*>(0x55);
  const char *mixer_name_1, *mixer_name_2;
  const char *devices[] = { "Dev1", "Comment for Dev1", "Dev2",
                            "Comment for Dev2" };

  ResetStubData();

  fake_list["_devices/HiFi"] = devices;
  fake_list_size["_devices/HiFi"] = 4;
  std::string id_1 = "=MixerName/Dev1/HiFi";
  std::string id_2 = "=MixerName/Dev2/HiFi";
  std::string value_1 = "MixerName1";
  std::string value_2 = "MixerName2";

  snd_use_case_get_ret_value[id_1] = 0;
  snd_use_case_get_value[id_1] = value_1;
  snd_use_case_get_ret_value[id_2] = 0;
  snd_use_case_get_value[id_2] = value_2;
  mixer_name_1 = ucm_get_mixer_name_for_dev(mgr, "Dev1");
  mixer_name_2 = ucm_get_mixer_name_for_dev(mgr, "Dev2");

  EXPECT_EQ(0, strcmp(mixer_name_1, value_1.c_str()));
  EXPECT_EQ(0, strcmp(mixer_name_2, value_2.c_str()));
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
  snd_use_case_set_param.push_back(
      std::make_pair(std::string(identifier), std::string(value)));
  return snd_use_case_set_return;;
}

int snd_use_case_get_list(snd_use_case_mgr_t *uc_mgr,
                          const char *identifier,
                          const char **list[]) {
  *list = fake_list[identifier];
  return fake_list_size[identifier];
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
