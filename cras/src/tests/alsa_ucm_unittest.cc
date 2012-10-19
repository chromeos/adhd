// Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <gtest/gtest.h>
#include <iniparser.h>
#include <stdio.h>

extern "C" {
#include "cras_alsa_ucm.h"
#include "cras_types.h"
}

namespace {

static int snd_use_case_mgr_open_return;
static snd_use_case_mgr_t *snd_use_case_mgr_open_mgr_ptr;
static unsigned snd_use_case_mgr_open_called;
static unsigned snd_use_case_mgr_close_called;
static unsigned snd_use_case_get_called;
static char *snd_use_case_get_id;
static int snd_use_case_set_return;
static const char *snd_use_case_get_value;
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
  snd_use_case_get_id = NULL;
  snd_use_case_set_called = 0;
  snd_use_case_free_list_called = 0;
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
  const char *file_name;

  ResetStubData();

  snd_use_case_get_value = "EdidFileName";

  file_name = ucm_get_edid_file_for_dev(mgr, "Dev1");
  ASSERT_TRUE(file_name);
  EXPECT_EQ(0, strcmp(file_name, snd_use_case_get_value));
  free((void*)file_name);

  ASSERT_EQ(1, snd_use_case_get_called);
  ASSERT_TRUE(snd_use_case_get_id);
  EXPECT_EQ(0, strcmp(snd_use_case_get_id, "=EDIDFile/Dev1/HiFi"));
  free(snd_use_case_get_id);
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
  *value = strdup(snd_use_case_get_value);
  snd_use_case_get_id = strdup(identifier);
  return 0;
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
