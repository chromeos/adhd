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
static struct cras_alsa_card* kFakeAlsaCard;
size_t cras_alsa_card_create_called;
size_t cras_alsa_card_destroy_called;
static size_t add_stub_called;
static size_t rm_stub_called;
static size_t callback_stub_called;
static void *select_data_value;
static size_t add_callback_called;
static cras_alert_cb add_callback_cb;
static void *add_callback_arg;
static size_t rm_callback_called;
static cras_alert_cb rm_callback_cb;
static void *rm_callback_arg;
static size_t alert_pending_called;

static void ResetStubData() {
  cras_alsa_card_create_called = 0;
  cras_alsa_card_destroy_called = 0;
  kFakeAlsaCard = reinterpret_cast<struct cras_alsa_card*>(0x33);
  add_stub_called = 0;
  rm_stub_called = 0;
  callback_stub_called = 0;
  add_callback_called = 0;
  rm_callback_called = 0;
  alert_pending_called = 0;
}

static void volume_changed(void *arg) {
}

static void volume_limits_changed(void *arg) {
}

static void volume_limits_changed_2(void *arg) {
}

static void capture_gain_changed(void *arg) {
}

static void mute_changed(void *arg) {
}

static void capture_mute_changed(void *arg) {
}

static void capture_mute_changed_2(void *arg) {
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
  cras_system_state_init();
  EXPECT_EQ(100, cras_system_get_volume());
  EXPECT_EQ(2000, cras_system_get_capture_gain());
  EXPECT_EQ(0, cras_system_get_mute());
  EXPECT_EQ(0, cras_system_get_capture_mute());
  cras_system_state_deinit();
}

TEST(SystemStateSuite, SetVolume) {
  cras_system_state_init();
  cras_system_set_volume(0);
  EXPECT_EQ(0, cras_system_get_volume());
  cras_system_set_volume(50);
  EXPECT_EQ(50, cras_system_get_volume());
  cras_system_set_volume(CRAS_MAX_SYSTEM_VOLUME);
  EXPECT_EQ(CRAS_MAX_SYSTEM_VOLUME, cras_system_get_volume());
  cras_system_set_volume(CRAS_MAX_SYSTEM_VOLUME + 1);
  EXPECT_EQ(CRAS_MAX_SYSTEM_VOLUME, cras_system_get_volume());
  cras_system_state_deinit();
}

TEST(SystemStateSuite, SetMinMaxVolume) {
  cras_system_state_init();
  cras_system_set_volume_limits(-10000, -600);
  EXPECT_EQ(-10000, cras_system_get_min_volume());
  EXPECT_EQ(-600, cras_system_get_max_volume());
  cras_system_state_deinit();
}

TEST(SystemStateSuite, SetCaptureVolume) {
  cras_system_state_init();
  cras_system_set_capture_gain(0);
  EXPECT_EQ(0, cras_system_get_capture_gain());
  cras_system_set_capture_gain(3000);
  EXPECT_EQ(3000, cras_system_get_capture_gain());
  // Check that it is limited to the minimum allowed gain.
  cras_system_set_capture_gain(-10000);
  EXPECT_EQ(-5000, cras_system_get_capture_gain());
  cras_system_state_deinit();
}

TEST(SystemStateSuite, VolumeChangedCallback) {
  void * const fake_user_arg = (void *)1;
  const size_t fake_volume = 55;
  const size_t fake_volume_2 = 44;
  int rc;

  cras_system_state_init();
  ResetStubData();
  cras_system_register_volume_changed_cb(volume_changed, fake_user_arg);
  EXPECT_EQ(1, add_callback_called);
  EXPECT_EQ((void *)volume_changed, (void *)add_callback_cb);
  EXPECT_EQ(fake_user_arg, add_callback_arg);

  cras_system_set_volume(fake_volume);
  EXPECT_EQ(fake_volume, cras_system_get_volume());
  EXPECT_EQ(1, alert_pending_called);

  rc = cras_system_remove_volume_changed_cb(volume_changed, fake_user_arg);
  EXPECT_EQ(0, rc);
  EXPECT_EQ(1, rm_callback_called);
  EXPECT_EQ((void *)volume_changed, (void *)rm_callback_cb);
  EXPECT_EQ(fake_user_arg, rm_callback_arg);

  cras_system_set_volume(fake_volume_2);
  EXPECT_EQ(fake_volume_2, cras_system_get_volume());
  EXPECT_EQ(2, alert_pending_called);
  cras_system_state_deinit();
}

TEST(SystemStateSuite, VolumeLimitChangedCallbackMultiple) {
  void * const fake_user_arg = (void *)1;
  void * const fake_user_arg_2 = (void *)2;
  const size_t fake_min = -10000;
  const size_t fake_max = 800;
  const size_t fake_min_2 = -4500;
  const size_t fake_max_2 = -600;
  int rc;

  cras_system_state_init();
  ResetStubData();
  rc = cras_system_register_volume_limits_changed_cb(volume_limits_changed,
                                                     fake_user_arg);
  EXPECT_EQ(0, rc);
  EXPECT_EQ(1, add_callback_called);
  EXPECT_EQ((void *)volume_limits_changed, (void *)add_callback_cb);
  EXPECT_EQ(fake_user_arg, add_callback_arg);

  cras_system_register_volume_limits_changed_cb(volume_limits_changed_2,
                                                fake_user_arg_2);
  EXPECT_EQ(2, add_callback_called);
  EXPECT_EQ((void *)volume_limits_changed_2, (void *)add_callback_cb);
  EXPECT_EQ(fake_user_arg_2, add_callback_arg);

  cras_system_set_volume_limits(fake_min, fake_max);
  cras_system_set_capture_gain_limits(fake_min_2, fake_max_2);
  EXPECT_EQ(fake_min, cras_system_get_min_volume());
  EXPECT_EQ(fake_max, cras_system_get_max_volume());
  EXPECT_EQ(2, alert_pending_called);

  cras_system_remove_volume_limits_changed_cb(volume_limits_changed,
                                              fake_user_arg);
  EXPECT_EQ(1, rm_callback_called);
  EXPECT_EQ((void *)volume_limits_changed, (void *)rm_callback_cb);
  EXPECT_EQ(fake_user_arg, rm_callback_arg);

  cras_system_set_volume_limits(fake_min_2, fake_max_2);
  EXPECT_EQ(fake_min_2, cras_system_get_min_volume());
  EXPECT_EQ(fake_max_2, cras_system_get_max_volume());
  EXPECT_EQ(3, alert_pending_called);

  cras_system_remove_volume_limits_changed_cb(volume_limits_changed_2,
                                              fake_user_arg_2);

  cras_system_set_volume_limits(fake_min, fake_max);
  EXPECT_EQ(fake_min, cras_system_get_min_volume());
  EXPECT_EQ(fake_max, cras_system_get_max_volume());
  EXPECT_EQ(4, alert_pending_called);
  cras_system_state_deinit();
}

TEST(SystemStateSuite, CaptureVolumeChangedCallback) {
  void * const fake_user_arg = (void *)1;
  const long fake_capture_gain = 2200;
  const long fake_capture_gain_2 = -1600;
  int rc;

  cras_system_state_init();
  ResetStubData();
  cras_system_register_capture_gain_changed_cb(capture_gain_changed,
                                               fake_user_arg);
  EXPECT_EQ(1, add_callback_called);
  EXPECT_EQ((void *)capture_gain_changed, (void *)add_callback_cb);
  EXPECT_EQ(fake_user_arg, add_callback_arg);

  cras_system_set_capture_gain(fake_capture_gain);
  EXPECT_EQ(fake_capture_gain, cras_system_get_capture_gain());
  EXPECT_EQ(1, alert_pending_called);

  rc = cras_system_remove_capture_gain_changed_cb(capture_gain_changed,
                                                  fake_user_arg);
  EXPECT_EQ(0, rc);
  EXPECT_EQ(1, rm_callback_called);
  EXPECT_EQ((void *)capture_gain_changed, (void *)rm_callback_cb);
  EXPECT_EQ(fake_user_arg, rm_callback_arg);

  cras_system_set_capture_gain(fake_capture_gain_2);
  EXPECT_EQ(fake_capture_gain_2, cras_system_get_capture_gain());
  EXPECT_EQ(2, alert_pending_called);
  cras_system_state_deinit();
}

TEST(SystemStateSuite, SetMute) {
  cras_system_state_init();
  EXPECT_EQ(0, cras_system_get_mute());
  cras_system_set_mute(0);
  EXPECT_EQ(0, cras_system_get_mute());
  cras_system_set_mute(1);
  EXPECT_EQ(1, cras_system_get_mute());
  cras_system_set_mute(22);
  EXPECT_EQ(1, cras_system_get_mute());
  cras_system_state_deinit();
}

TEST(SystemStateSuite, MuteChangedCallback) {
  void * const fake_user_arg = (void *)1;
  int rc;

  cras_system_state_init();
  ResetStubData();
  cras_system_register_mute_changed_cb(mute_changed, fake_user_arg);
  EXPECT_EQ(1, add_callback_called);
  EXPECT_EQ((void *)mute_changed, (void *)add_callback_cb);
  EXPECT_EQ(fake_user_arg, add_callback_arg);

  cras_system_set_mute(1);
  EXPECT_EQ(1, cras_system_get_mute());
  EXPECT_EQ(1, alert_pending_called);

  rc = cras_system_remove_mute_changed_cb(mute_changed, fake_user_arg);
  EXPECT_EQ(0, rc);
  EXPECT_EQ(1, rm_callback_called);
  EXPECT_EQ((void *)mute_changed, (void *)rm_callback_cb);
  EXPECT_EQ(fake_user_arg, rm_callback_arg);

  cras_system_set_mute(0);
  EXPECT_EQ(0, cras_system_get_mute());
  EXPECT_EQ(2, alert_pending_called);
  cras_system_state_deinit();
}

TEST(SystemStateSuite, CaptureMuteChangedCallbackMultiple) {
  void * const fake_arg = (void *)1;
  void * const fake_arg_2 = (void *)2;
  int rc;

  cras_system_state_init();
  ResetStubData();
  rc = cras_system_register_capture_mute_changed_cb(capture_mute_changed,
                                                    fake_arg);
  EXPECT_EQ(0, rc);
  EXPECT_EQ(1, add_callback_called);
  EXPECT_EQ((void *)capture_mute_changed, (void *)add_callback_cb);
  EXPECT_EQ(fake_arg, add_callback_arg);

  rc = cras_system_register_capture_mute_changed_cb(capture_mute_changed_2,
                                                    fake_arg_2);
  EXPECT_EQ(0, rc);
  EXPECT_EQ(2, add_callback_called);
  EXPECT_EQ((void *)capture_mute_changed_2, (void *)add_callback_cb);
  EXPECT_EQ(fake_arg_2, add_callback_arg);

  cras_system_set_capture_mute(1);
  EXPECT_EQ(1, cras_system_get_capture_mute());
  EXPECT_EQ(1, alert_pending_called);

  rc = cras_system_remove_capture_mute_changed_cb(capture_mute_changed,
                                                  fake_arg);
  EXPECT_EQ(0, rc);
  EXPECT_EQ(1, rm_callback_called);
  EXPECT_EQ((void *)capture_mute_changed, (void *)rm_callback_cb);
  EXPECT_EQ(fake_arg, rm_callback_arg);

  cras_system_set_capture_mute(0);
  EXPECT_EQ(0, cras_system_get_capture_mute());
  EXPECT_EQ(2, alert_pending_called);

  rc = cras_system_remove_capture_mute_changed_cb(capture_mute_changed_2,
                                                  fake_arg_2);
  EXPECT_EQ(0, rc);
  cras_system_state_deinit();
}

TEST(SystemStateSuite, MuteLocked) {
  void * const fake_user_arg = (void *)1;
  int rc;

  cras_system_state_init();
  ResetStubData();

  cras_system_register_mute_changed_cb(mute_changed, fake_user_arg);
  EXPECT_EQ(1, add_callback_called);
  EXPECT_EQ((void *)mute_changed, (void *)add_callback_cb);
  EXPECT_EQ(fake_user_arg, add_callback_arg);

  cras_system_set_mute(1);
  EXPECT_EQ(1, cras_system_get_mute());
  EXPECT_EQ(0, cras_system_get_mute_locked());
  EXPECT_EQ(1, alert_pending_called);

  cras_system_set_mute_locked(1);
  cras_system_set_mute(0);
  EXPECT_EQ(1, cras_system_get_mute());
  EXPECT_EQ(1, cras_system_get_mute_locked());
  EXPECT_EQ(1, alert_pending_called);

  rc = cras_system_remove_mute_changed_cb(mute_changed, fake_user_arg);
  EXPECT_EQ(0, rc);
  EXPECT_EQ(1, rm_callback_called);
  EXPECT_EQ((void *)mute_changed, (void *)rm_callback_cb);
  EXPECT_EQ(fake_user_arg, rm_callback_arg);

  cras_system_register_capture_mute_changed_cb(capture_mute_changed,
                                               fake_user_arg);
  cras_system_set_capture_mute(1);
  EXPECT_EQ(1, cras_system_get_capture_mute());
  EXPECT_EQ(0, cras_system_get_capture_mute_locked());
  EXPECT_EQ(2, alert_pending_called);

  cras_system_set_capture_mute_locked(1);
  cras_system_set_capture_mute(0);
  EXPECT_EQ(1, cras_system_get_capture_mute());
  EXPECT_EQ(1, cras_system_get_capture_mute_locked());
  EXPECT_EQ(2, alert_pending_called);
  cras_system_state_deinit();
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
  cras_system_state_deinit();
}

TEST(SystemSettingsStreamCount, StreamCount) {
  ResetStubData();
  cras_system_state_init();

  EXPECT_EQ(0, cras_system_state_get_active_streams());
  cras_system_state_stream_added();
  EXPECT_EQ(1, cras_system_state_get_active_streams());
  struct cras_timespec ts1;
  cras_system_state_get_last_stream_active_time(&ts1);
  cras_system_state_stream_removed();
  EXPECT_EQ(0, cras_system_state_get_active_streams());
  struct cras_timespec ts2;
  cras_system_state_get_last_stream_active_time(&ts2);
  EXPECT_NE(0, memcmp(&ts1, &ts2, sizeof(ts1)));
  cras_system_state_deinit();
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

struct cras_alert *cras_alert_create(cras_alert_prepare prepare)
{
  return NULL;
}

void cras_alert_destroy(struct cras_alert *alert)
{
}

int cras_alert_add_callback(struct cras_alert *alert, cras_alert_cb cb,
			    void *arg)
{
  add_callback_called++;
  add_callback_cb = cb;
  add_callback_arg = arg;
  return 0;
}

int cras_alert_rm_callback(struct cras_alert *alert, cras_alert_cb cb,
			   void *arg)
{
  rm_callback_called++;
  rm_callback_cb = cb;
  rm_callback_arg = arg;
  return 0;
}

void cras_alert_pending(struct cras_alert *alert)
{
  alert_pending_called++;
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
