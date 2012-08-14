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
static int snd_use_case_set_return;
static unsigned snd_use_case_set_called;

static void ResetStubData() {
  snd_use_case_mgr_open_called = 0;
  snd_use_case_mgr_open_return = 0;
  snd_use_case_mgr_close_called = 0;
  snd_use_case_set_return = 0;
  snd_use_case_set_called = 0;
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

int snd_use_case_set(snd_use_case_mgr_t* uc_mgr,
                     const char *identifier,
                     const char *value) {
  snd_use_case_set_called++;
  return snd_use_case_set_return;;
}

} /* extern "C" */

}  //  namespace

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
