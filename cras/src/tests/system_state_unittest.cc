// Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stdio.h>
#include <gtest/gtest.h>

extern "C" {
#include "cras_system_state.h"
#include "cras_types.h"
}

namespace {
size_t volume_changed_called;
size_t volume_changed_value;
void *volume_changed_arg_value;
size_t volume_changed_2_called;
size_t volume_changed_2_value;
void *volume_changed_2_arg_value;
size_t volume_limits_changed_called;
size_t volume_limits_changed_2_called;
size_t capture_gain_changed_called;
long capture_gain_changed_value;
void *capture_gain_changed_arg_value;
size_t capture_gain_changed_2_called;
long capture_gain_changed_2_value;
void *capture_gain_changed_2_arg_value;
size_t mute_changed_called;
size_t mute_changed_2_called;
size_t mute_changed_value;
void *mute_changed_arg_value;
size_t capture_mute_changed_called;
size_t capture_mute_changed_2_called;
size_t capture_mute_changed_value;
void *capture_mute_changed_arg_value;
static struct cras_alsa_card* kFakeAlsaCard;
size_t cras_alsa_card_create_called;
size_t cras_alsa_card_destroy_called;
static size_t add_stub_called;
static size_t rm_stub_called;
static size_t callback_stub_called;
static void *select_data_value;

static void ResetStubData() {
  volume_changed_called = 0;
  volume_changed_value = 0;
  volume_changed_2_called = 0;
  volume_changed_2_value = 0;
  volume_limits_changed_called = 0;
  volume_limits_changed_2_called = 0;
  capture_gain_changed_called = 0;
  capture_gain_changed_value = 0;
  capture_gain_changed_2_called = 0;
  capture_gain_changed_2_value = 0;
  mute_changed_called = 0;
  mute_changed_2_called = 0;
  mute_changed_value = 0;
  capture_mute_changed_called = 0;
  capture_mute_changed_2_called = 0;
  capture_mute_changed_value = 0;
  cras_alsa_card_create_called = 0;
  cras_alsa_card_destroy_called = 0;
  kFakeAlsaCard = reinterpret_cast<struct cras_alsa_card*>(0x33);
  add_stub_called = 0;
  rm_stub_called = 0;
  callback_stub_called = 0;
}

static void volume_changed(void *arg) {
  volume_changed_called++;
  volume_changed_value = cras_system_get_volume();
  volume_changed_arg_value = arg;
}

static void volume_changed_2(void *arg) {
  volume_changed_2_called++;
  volume_changed_2_value = cras_system_get_volume();
  volume_changed_2_arg_value = arg;
}

static void volume_limits_changed(void *arg) {
  volume_limits_changed_called++;
}

static void volume_limits_changed_2(void *arg) {
  volume_limits_changed_2_called++;
}

static void capture_gain_changed(void *arg) {
  capture_gain_changed_called++;
  capture_gain_changed_value = cras_system_get_capture_gain();
  capture_gain_changed_arg_value = arg;
}

static void capture_gain_changed_2(void *arg) {
  capture_gain_changed_2_called++;
  capture_gain_changed_2_value = cras_system_get_capture_gain();
  capture_gain_changed_2_arg_value = arg;
}

static void mute_changed(void *arg) {
  mute_changed_called++;
  mute_changed_value = cras_system_get_mute();
  mute_changed_arg_value = arg;
}

static void mute_changed_2(void *arg) {
  mute_changed_2_called++;
}

static void capture_mute_changed(void *arg) {
  capture_mute_changed_called++;
  capture_mute_changed_value = cras_system_get_capture_mute();
  capture_mute_changed_arg_value = arg;
}

static void capture_mute_changed_2(void *arg) {
  capture_mute_changed_2_called++;
}

static int add_stub(int fd, void (*cb)(void *data),
                    void *callback_data, void *select_data) {
  add_stub_called++;
  select_data_value = select_data;
  return 0;
}

static void rm_stub(int fd, void *select_data) {
  rm_stub_called++;
  select_data_value = select_data;
}

static void callback_stub(void *data) {
  callback_stub_called++;
}

TEST(SystemStateSuite, DefaultVolume) {
  cras_system_state_deinit();
  cras_system_state_init();
  EXPECT_EQ(100, cras_system_get_volume());
  EXPECT_EQ(2000, cras_system_get_capture_gain());
  EXPECT_EQ(0, cras_system_get_mute());
  EXPECT_EQ(0, cras_system_get_capture_mute());
}

TEST(SystemStateSuite, SetVolume) {
  cras_system_state_deinit();
  cras_system_state_init();
  cras_system_set_volume(0);
  EXPECT_EQ(0, cras_system_get_volume());
  cras_system_set_volume(50);
  EXPECT_EQ(50, cras_system_get_volume());
  cras_system_set_volume(CRAS_MAX_SYSTEM_VOLUME);
  EXPECT_EQ(CRAS_MAX_SYSTEM_VOLUME, cras_system_get_volume());
  cras_system_set_volume(CRAS_MAX_SYSTEM_VOLUME + 1);
  EXPECT_EQ(CRAS_MAX_SYSTEM_VOLUME, cras_system_get_volume());
}

TEST(SystemStateSuite, SetMinMaxVolume) {
  cras_system_state_deinit();
  cras_system_state_init();
  cras_system_set_volume_limits(-10000, -600);
  EXPECT_EQ(-10000, cras_system_get_min_volume());
  EXPECT_EQ(-600, cras_system_get_max_volume());
}

TEST(SystemStateSuite, SetCaptureVolume) {
  cras_system_state_deinit();
  cras_system_state_init();
  cras_system_set_capture_gain(0);
  EXPECT_EQ(0, cras_system_get_capture_gain());
  cras_system_set_capture_gain(3000);
  EXPECT_EQ(3000, cras_system_get_capture_gain());
  // Check that it is limited to the minimum allowed gain.
  cras_system_set_capture_gain(-10000);
  EXPECT_EQ(-5000, cras_system_get_capture_gain());
}

TEST(SystemStateSuite, VolumeChangedCallback) {
  void * const fake_user_arg = (void *)1;
  const size_t fake_volume = 55;
  const size_t fake_volume_2 = 44;
  int rc;

  cras_system_state_deinit();
  cras_system_state_init();
  cras_system_register_volume_changed_cb(volume_changed, fake_user_arg);
  volume_changed_called = 0;
  cras_system_set_volume(fake_volume);
  EXPECT_EQ(fake_volume, cras_system_get_volume());
  EXPECT_EQ(1, volume_changed_called);
  EXPECT_EQ(fake_volume, volume_changed_value);
  EXPECT_EQ(fake_user_arg, volume_changed_arg_value);

  rc = cras_system_register_volume_changed_cb(NULL, NULL);
  EXPECT_EQ(-EINVAL, rc);
  rc = cras_system_remove_volume_changed_cb(volume_changed, fake_user_arg);
  EXPECT_EQ(0, rc);
  volume_changed_called = 0;
  cras_system_set_volume(fake_volume_2);
  EXPECT_EQ(fake_volume_2, cras_system_get_volume());
  EXPECT_EQ(0, volume_changed_called);
}

TEST(SystemStateSuite, VolumeChangedCallbackMultiple) {
  void * const fake_user_arg = (void *)1;
  void * const fake_user_arg_2 = (void *)2;
  const size_t fake_volume = 55;
  const size_t fake_volume_2 = 44;
  int rc;

  cras_system_state_deinit();
  cras_system_state_init();
  rc = cras_system_register_volume_changed_cb(volume_changed, fake_user_arg);
  EXPECT_EQ(0, rc);
  rc = cras_system_register_volume_changed_cb(volume_changed, fake_user_arg);
  EXPECT_EQ(-EEXIST, rc);
  cras_system_register_volume_changed_cb(volume_changed_2, fake_user_arg_2);
  volume_changed_called = 0;
  volume_changed_2_called = 0;
  cras_system_set_volume(fake_volume);
  EXPECT_EQ(fake_volume, cras_system_get_volume());
  EXPECT_EQ(1, volume_changed_called);
  EXPECT_EQ(1, volume_changed_2_called);
  EXPECT_EQ(fake_volume, volume_changed_value);
  EXPECT_EQ(fake_user_arg, volume_changed_arg_value);
  EXPECT_EQ(fake_volume, volume_changed_2_value);
  EXPECT_EQ(fake_user_arg_2, volume_changed_2_arg_value);

  rc = cras_system_remove_volume_changed_cb(volume_changed, fake_user_arg_2);
  EXPECT_EQ(-ENOENT, rc);

  cras_system_remove_volume_changed_cb(volume_changed, fake_user_arg);
  volume_changed_called = 0;
  volume_changed_2_called = 0;
  cras_system_set_volume(fake_volume_2);
  EXPECT_EQ(fake_volume_2, cras_system_get_volume());
  EXPECT_EQ(0, volume_changed_called);
  EXPECT_EQ(1, volume_changed_2_called);
  EXPECT_EQ(fake_volume_2, volume_changed_2_value);
  EXPECT_EQ(fake_user_arg_2, volume_changed_2_arg_value);

  cras_system_remove_volume_changed_cb(volume_changed_2, fake_user_arg_2);
  volume_changed_called = 0;
  volume_changed_2_called = 0;
  cras_system_set_volume(fake_volume);
  EXPECT_EQ(fake_volume, cras_system_get_volume());
  EXPECT_EQ(0, volume_changed_called);
  EXPECT_EQ(0, volume_changed_2_called);

  rc = cras_system_remove_volume_changed_cb(volume_changed_2, fake_user_arg_2);
  EXPECT_EQ(-ENOENT, rc);
}

TEST(SystemStateSuite, VolumeLimitChangedCallbackMultiple) {
  void * const fake_user_arg = (void *)1;
  void * const fake_user_arg_2 = (void *)2;
  const size_t fake_min = -10000;
  const size_t fake_max = 800;
  const size_t fake_min_2 = -4500;
  const size_t fake_max_2 = -600;
  int rc;

  cras_system_state_deinit();
  cras_system_state_init();
  rc = cras_system_register_volume_limits_changed_cb(volume_limits_changed,
                                                     fake_user_arg);
  EXPECT_EQ(0, rc);
  rc = cras_system_register_volume_limits_changed_cb(volume_limits_changed,
                                                     fake_user_arg);
  EXPECT_EQ(-EEXIST, rc);
  cras_system_register_volume_limits_changed_cb(volume_limits_changed_2,
                                                fake_user_arg_2);
  volume_limits_changed_called = 0;
  volume_limits_changed_2_called = 0;
  cras_system_set_volume_limits(fake_min, fake_max);
  cras_system_set_capture_gain_limits(fake_min_2, fake_max_2);
  EXPECT_EQ(fake_min, cras_system_get_min_volume());
  EXPECT_EQ(fake_max, cras_system_get_max_volume());
  EXPECT_EQ(fake_min_2, cras_system_get_min_capture_gain());
  EXPECT_EQ(fake_max_2, cras_system_get_max_capture_gain());
  EXPECT_EQ(2, volume_limits_changed_called);
  EXPECT_EQ(2, volume_limits_changed_2_called);

  rc = cras_system_remove_volume_limits_changed_cb(volume_limits_changed,
                                                   fake_user_arg_2);
  EXPECT_EQ(-ENOENT, rc);

  cras_system_remove_volume_limits_changed_cb(volume_limits_changed,
                                              fake_user_arg);
  volume_limits_changed_called = 0;
  volume_limits_changed_2_called = 0;
  cras_system_set_volume_limits(fake_min_2, fake_max_2);
  EXPECT_EQ(fake_min_2, cras_system_get_min_volume());
  EXPECT_EQ(fake_max_2, cras_system_get_max_volume());
  EXPECT_EQ(0, volume_limits_changed_called);
  EXPECT_EQ(1, volume_limits_changed_2_called);

  cras_system_remove_volume_limits_changed_cb(volume_limits_changed_2,
                                              fake_user_arg_2);
  volume_limits_changed_called = 0;
  volume_limits_changed_2_called = 0;
  cras_system_set_volume_limits(fake_min, fake_max);
  EXPECT_EQ(fake_min, cras_system_get_min_volume());
  EXPECT_EQ(fake_max, cras_system_get_max_volume());
  EXPECT_EQ(0, volume_limits_changed_called);
  EXPECT_EQ(0, volume_limits_changed_2_called);

  rc = cras_system_remove_volume_limits_changed_cb(volume_limits_changed_2,
                                                   fake_user_arg_2);
  EXPECT_EQ(-ENOENT, rc);
}

TEST(SystemStateSuite, CaptureVolumeChangedCallback) {
  void * const fake_user_arg = (void *)1;
  const long fake_capture_gain = 2200;
  const long fake_capture_gain_2 = -1600;
  int rc;

  cras_system_state_deinit();
  cras_system_state_init();
  cras_system_register_capture_gain_changed_cb(capture_gain_changed,
                                                 fake_user_arg);
  capture_gain_changed_called = 0;
  cras_system_set_capture_gain(fake_capture_gain);
  EXPECT_EQ(fake_capture_gain, cras_system_get_capture_gain());
  EXPECT_EQ(1, capture_gain_changed_called);
  EXPECT_EQ(fake_capture_gain, capture_gain_changed_value);
  EXPECT_EQ(fake_user_arg, capture_gain_changed_arg_value);

  rc = cras_system_register_capture_gain_changed_cb(NULL, NULL);
  EXPECT_EQ(-EINVAL, rc);
  rc = cras_system_remove_capture_gain_changed_cb(capture_gain_changed,
                                                  fake_user_arg);
  EXPECT_EQ(0, rc);
  capture_gain_changed_called = 0;
  cras_system_set_capture_gain(fake_capture_gain_2);
  EXPECT_EQ(fake_capture_gain_2, cras_system_get_capture_gain());
  EXPECT_EQ(0, capture_gain_changed_called);
}

TEST(SystemStateSuite, CaptureVolumeChangedCallbackMultiple) {
  void * const fake_user_arg = (void *)1;
  void * const fake_user_arg_2 = (void *)2;
  const size_t fake_capture_gain = -100;
  const size_t fake_capture_gain_2 = 400;
  int rc;

  cras_system_state_deinit();
  cras_system_state_init();
  rc = cras_system_register_capture_gain_changed_cb(capture_gain_changed,
                                                    fake_user_arg);
  EXPECT_EQ(0, rc);
  rc = cras_system_register_capture_gain_changed_cb(capture_gain_changed,
                                                    fake_user_arg);
  EXPECT_EQ(-EEXIST, rc);
  cras_system_register_capture_gain_changed_cb(capture_gain_changed_2,
                                               fake_user_arg_2);
  capture_gain_changed_called = 0;
  capture_gain_changed_2_called = 0;
  cras_system_set_capture_gain(fake_capture_gain);
  EXPECT_EQ(fake_capture_gain, cras_system_get_capture_gain());
  EXPECT_EQ(1, capture_gain_changed_called);
  EXPECT_EQ(1, capture_gain_changed_2_called);
  EXPECT_EQ(fake_capture_gain, capture_gain_changed_value);
  EXPECT_EQ(fake_user_arg, capture_gain_changed_arg_value);
  EXPECT_EQ(fake_capture_gain, capture_gain_changed_2_value);
  EXPECT_EQ(fake_user_arg_2, capture_gain_changed_2_arg_value);

  rc = cras_system_remove_capture_gain_changed_cb(capture_gain_changed,
                                                   fake_user_arg_2);
  EXPECT_EQ(-ENOENT, rc);

  cras_system_remove_capture_gain_changed_cb(capture_gain_changed,
                                             fake_user_arg);
  capture_gain_changed_called = 0;
  capture_gain_changed_2_called = 0;
  cras_system_set_capture_gain(fake_capture_gain_2);
  EXPECT_EQ(fake_capture_gain_2, cras_system_get_capture_gain());
  EXPECT_EQ(0, capture_gain_changed_called);
  EXPECT_EQ(1, capture_gain_changed_2_called);
  EXPECT_EQ(fake_capture_gain_2, capture_gain_changed_2_value);
  EXPECT_EQ(fake_user_arg_2, capture_gain_changed_2_arg_value);

  cras_system_remove_capture_gain_changed_cb(capture_gain_changed_2,
                                             fake_user_arg_2);
  capture_gain_changed_called = 0;
  capture_gain_changed_2_called = 0;
  cras_system_set_capture_gain(fake_capture_gain);
  EXPECT_EQ(fake_capture_gain, cras_system_get_capture_gain());
  EXPECT_EQ(0, capture_gain_changed_called);
  EXPECT_EQ(0, capture_gain_changed_2_called);

  rc = cras_system_remove_capture_gain_changed_cb(capture_gain_changed_2,
                                                  fake_user_arg_2);
  EXPECT_EQ(-ENOENT, rc);
}

TEST(SystemStateSuite, SetMute) {
  cras_system_state_deinit();
  cras_system_state_init();
  EXPECT_EQ(0, cras_system_get_mute());
  cras_system_set_mute(0);
  EXPECT_EQ(0, cras_system_get_mute());
  cras_system_set_mute(1);
  EXPECT_EQ(1, cras_system_get_mute());
  cras_system_set_mute(22);
  EXPECT_EQ(1, cras_system_get_mute());
}

TEST(SystemStateSuite, MuteChangedCallback) {
  void * const fake_user_arg = (void *)1;
  int rc;

  cras_system_state_deinit();
  cras_system_state_init();
  cras_system_register_volume_changed_cb(volume_changed, fake_user_arg);
  cras_system_register_mute_changed_cb(mute_changed, fake_user_arg);
  mute_changed_called = 0;
  cras_system_set_mute(1);
  EXPECT_EQ(1, cras_system_get_mute());
  EXPECT_EQ(1, mute_changed_called);
  EXPECT_EQ(1, mute_changed_value);
  EXPECT_EQ(fake_user_arg, mute_changed_arg_value);
  EXPECT_EQ(0, volume_changed_called);

  rc = cras_system_register_mute_changed_cb(NULL, NULL);
  EXPECT_EQ(-EINVAL, rc);
  rc = cras_system_remove_mute_changed_cb(mute_changed, fake_user_arg);
  EXPECT_EQ(0, rc);
  mute_changed_called = 0;
  cras_system_set_mute(0);
  EXPECT_EQ(0, cras_system_get_mute());
  EXPECT_EQ(0, mute_changed_called);
}

TEST(SystemStateSuite, MuteChangedCallbackMultiple) {
  void * const fake_user_arg = (void *)1;
  void * const fake_user_arg_2 = (void *)2;
  int rc;

  cras_system_state_deinit();
  cras_system_state_init();
  cras_system_register_volume_changed_cb(volume_changed, fake_user_arg);
  rc = cras_system_register_mute_changed_cb(mute_changed, fake_user_arg);
  EXPECT_EQ(0, rc);
  rc = cras_system_register_mute_changed_cb(mute_changed, fake_user_arg);
  EXPECT_EQ(-EEXIST, rc);
  rc = cras_system_register_mute_changed_cb(mute_changed_2, fake_user_arg_2);
  EXPECT_EQ(0, rc);

  mute_changed_called = 0;
  mute_changed_2_called = 0;
  cras_system_set_mute(1);
  EXPECT_EQ(1, cras_system_get_mute());
  EXPECT_EQ(1, mute_changed_called);
  EXPECT_EQ(1, mute_changed_2_called);
  EXPECT_EQ(1, mute_changed_value);
  EXPECT_EQ(fake_user_arg, mute_changed_arg_value);
  EXPECT_EQ(0, volume_changed_called);

  rc = cras_system_remove_mute_changed_cb(mute_changed, fake_user_arg_2);
  EXPECT_EQ(-ENOENT, rc);
  rc = cras_system_remove_mute_changed_cb(mute_changed, fake_user_arg);
  EXPECT_EQ(0, rc);
  mute_changed_called = 0;
  mute_changed_2_called = 0;
  cras_system_set_mute(0);
  EXPECT_EQ(0, cras_system_get_mute());
  EXPECT_EQ(0, mute_changed_called);
  EXPECT_EQ(1, mute_changed_2_called);
  rc = cras_system_remove_mute_changed_cb(mute_changed_2, fake_user_arg_2);
  EXPECT_EQ(0, rc);
  rc = cras_system_remove_mute_changed_cb(mute_changed_2, fake_user_arg_2);
  EXPECT_EQ(-ENOENT, rc);
}

TEST(SystemStateSuite, CaptureMuteChangedCallbackMultiple) {
  void * const fake_arg = (void *)1;
  void * const fake_arg_2 = (void *)2;
  int rc;

  cras_system_state_deinit();
  cras_system_state_init();
  rc = cras_system_register_capture_mute_changed_cb(capture_mute_changed,
                                                    fake_arg);
  EXPECT_EQ(0, rc);
  rc = cras_system_register_capture_mute_changed_cb(
      capture_mute_changed,
      fake_arg);
  EXPECT_EQ(-EEXIST, rc);
  rc = cras_system_register_capture_mute_changed_cb(
      capture_mute_changed_2,
      fake_arg_2);
  EXPECT_EQ(0, rc);

  capture_mute_changed_called = 0;
  capture_mute_changed_2_called = 0;
  cras_system_set_capture_mute(1);
  EXPECT_EQ(1, cras_system_get_capture_mute());
  EXPECT_EQ(1, capture_mute_changed_called);
  EXPECT_EQ(1, capture_mute_changed_2_called);
  EXPECT_EQ(1, capture_mute_changed_value);
  EXPECT_EQ(fake_arg, capture_mute_changed_arg_value);
  EXPECT_EQ(0, volume_changed_called);

  rc = cras_system_remove_capture_mute_changed_cb(capture_mute_changed,
                                                  fake_arg_2);
  EXPECT_EQ(-ENOENT, rc);
  rc = cras_system_remove_capture_mute_changed_cb(capture_mute_changed,
                                                  fake_arg);
  EXPECT_EQ(0, rc);
  capture_mute_changed_called = 0;
  capture_mute_changed_2_called = 0;
  cras_system_set_capture_mute(0);
  EXPECT_EQ(0, cras_system_get_capture_mute());
  EXPECT_EQ(0, capture_mute_changed_called);
  EXPECT_EQ(1, capture_mute_changed_2_called);
  rc = cras_system_remove_capture_mute_changed_cb(capture_mute_changed_2,
                                                  fake_arg_2);
  EXPECT_EQ(0, rc);
  rc = cras_system_remove_capture_mute_changed_cb(capture_mute_changed_2,
                                                  fake_arg_2);
  EXPECT_EQ(-ENOENT, rc);
}

TEST(SystemStateSuite, MuteLocked) {
  void * const fake_user_arg = (void *)1;
  int rc;

  cras_system_state_deinit();
  cras_system_state_init();
  cras_system_register_volume_changed_cb(volume_changed, fake_user_arg);
  cras_system_register_mute_changed_cb(mute_changed, fake_user_arg);
  mute_changed_called = 0;
  cras_system_set_mute(1);
  EXPECT_EQ(1, cras_system_get_mute());
  EXPECT_EQ(0, cras_system_get_mute_locked());
  EXPECT_EQ(1, mute_changed_called);
  EXPECT_EQ(1, mute_changed_value);
  EXPECT_EQ(fake_user_arg, mute_changed_arg_value);
  EXPECT_EQ(0, volume_changed_called);
  cras_system_set_mute_locked(1);
  cras_system_set_mute(0);
  EXPECT_EQ(1, cras_system_get_mute());
  EXPECT_EQ(1, cras_system_get_mute_locked());
  EXPECT_EQ(1, mute_changed_called);
  EXPECT_EQ(1, mute_changed_value);
  EXPECT_EQ(fake_user_arg, mute_changed_arg_value);
  EXPECT_EQ(0, volume_changed_called);
  rc = cras_system_remove_mute_changed_cb(mute_changed, fake_user_arg);
  EXPECT_EQ(0, rc);

  cras_system_register_capture_mute_changed_cb(capture_mute_changed,
                                               fake_user_arg);
  mute_changed_called = 0;
  cras_system_set_capture_mute(1);
  EXPECT_EQ(1, cras_system_get_capture_mute());
  EXPECT_EQ(0, cras_system_get_capture_mute_locked());
  EXPECT_EQ(1, capture_mute_changed_called);
  EXPECT_EQ(1, capture_mute_changed_value);
  EXPECT_EQ(fake_user_arg, mute_changed_arg_value);
  cras_system_set_capture_mute_locked(1);
  cras_system_set_capture_mute(0);
  EXPECT_EQ(1, cras_system_get_capture_mute());
  EXPECT_EQ(1, cras_system_get_capture_mute_locked());
  EXPECT_EQ(1, capture_mute_changed_called);
  EXPECT_EQ(1, capture_mute_changed_value);
  EXPECT_EQ(fake_user_arg, mute_changed_arg_value);
}

TEST(SystemStateSuite, AddCardFailCreate) {
  ResetStubData();
  kFakeAlsaCard = NULL;
  cras_alsa_card_info info;

  info.card_type = ALSA_CARD_TYPE_INTERNAL;
  info.card_index = 0;
  EXPECT_EQ(-ENOMEM, cras_system_add_alsa_card(&info));
  EXPECT_EQ(1, cras_alsa_card_create_called);
}

TEST(SystemStateSuite, AddCard) {
  ResetStubData();
  cras_alsa_card_info info;

  info.card_type = ALSA_CARD_TYPE_INTERNAL;
  info.card_index = 0;
  EXPECT_EQ(0, cras_system_add_alsa_card(&info));
  EXPECT_EQ(1, cras_alsa_card_create_called);
  // Adding the same card again should fail.
  ResetStubData();
  EXPECT_NE(0, cras_system_add_alsa_card(&info));
  EXPECT_EQ(0, cras_alsa_card_create_called);
  // Removing card should destroy it.
  cras_system_remove_alsa_card(0);
  EXPECT_EQ(1, cras_alsa_card_destroy_called);
}

TEST(SystemSettingsRegisterSelectDescriptor, AddSelectFd) {
  void *stub_data = reinterpret_cast<void *>(44);
  void *select_data = reinterpret_cast<void *>(33);
  int rc;

  ResetStubData();
  cras_system_state_deinit();
  cras_system_state_init();
  rc = cras_system_add_select_fd(7, callback_stub, stub_data);
  EXPECT_NE(0, rc);
  EXPECT_EQ(0, add_stub_called);
  EXPECT_EQ(0, rm_stub_called);
  rc = cras_system_set_select_handler(add_stub, rm_stub, select_data);
  EXPECT_EQ(0, rc);
  EXPECT_EQ(0, add_stub_called);
  EXPECT_EQ(0, rm_stub_called);
  rc = cras_system_set_select_handler(add_stub, rm_stub, select_data);
  EXPECT_EQ(-EEXIST, rc);
  EXPECT_EQ(0, add_stub_called);
  EXPECT_EQ(0, rm_stub_called);
  rc = cras_system_add_select_fd(7, callback_stub, stub_data);
  EXPECT_EQ(0, rc);
  EXPECT_EQ(1, add_stub_called);
  EXPECT_EQ(select_data, select_data_value);
  cras_system_rm_select_fd(7);
  EXPECT_EQ(1, rm_stub_called);
  EXPECT_EQ(0, callback_stub_called);
  EXPECT_EQ(select_data, select_data_value);
}

TEST(SystemSettingsStreamCount, StreamCount) {
  ResetStubData();
  cras_system_state_deinit();
  cras_system_state_init();

  EXPECT_EQ(0, cras_system_state_get_active_streams());
  cras_system_state_stream_added();
  EXPECT_EQ(1, cras_system_state_get_active_streams());
  struct timespec ts1;
  cras_system_state_get_last_stream_active_time(&ts1);
  cras_system_state_stream_removed();
  EXPECT_EQ(0, cras_system_state_get_active_streams());
  struct timespec ts2;
  cras_system_state_get_last_stream_active_time(&ts2);
  EXPECT_NE(0, memcmp(&ts1, &ts2, sizeof(ts1)));
}

extern "C" {

struct cras_alsa_card *cras_alsa_card_create(struct cras_alsa_card_info *info) {
  cras_alsa_card_create_called++;
  return kFakeAlsaCard;
}

void cras_alsa_card_destroy(struct cras_alsa_card *alsa_card) {
  cras_alsa_card_destroy_called++;
}

size_t cras_alsa_card_get_index(const struct cras_alsa_card *alsa_card) {
  return 0;
}

struct cras_device_blacklist *cras_device_blacklist_create(
		const char *config_path)
{
	return NULL;
}

void cras_device_blacklist_destroy(struct cras_device_blacklist *blacklist)
{
}


cras_tm *cras_tm_init() {
  return static_cast<cras_tm*>(malloc(sizeof(unsigned int)));
}

void cras_tm_deinit(cras_tm *tm) {
  free(tm);
}

}  // extern "C"
}  // namespace

int main(int argc, char **argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
