// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <gtest/gtest.h>

#include "cras/src/tests/dbus_test.h"

extern "C" {
#include "cras/src/server/cras_dbus_control.h"
#include "cras_types.h"
}
static int num_channels_val;
static int audio_thread_config_global_remix_called;

namespace {

class DBusControlTestSuite : public DBusTest {
  virtual void SetUp() override {
    DBusTest::SetUp();
    cras_dbus_control_start(conn_);
    dbus_control_stub_reset();
  }
  virtual void TearDown() override {
    cras_dbus_control_stop();
    DBusTest::TearDown();
  }
  void dbus_control_stub_reset() {
    num_channels_val = 0;
    audio_thread_config_global_remix_called = 0;
  }
};

TEST_F(DBusControlTestSuite, SetGlobalOutputChannelRemixBasic) {
  // num_channels*num_channels == channels_map size
  int num_channels_val_sended = 2;
  std::vector<double> channels_map_val_sended = {0.1, 0.9, 0.9, 0.1};
  CreateMessageCall(CRAS_ROOT_OBJECT_PATH, CRAS_CONTROL_INTERFACE,
                    "SetGlobalOutputChannelRemix")
      .WithInt32(num_channels_val_sended)
      .WithArrayOfDouble(channels_map_val_sended)
      .Send();
  WaitForMatches();
  EXPECT_EQ(num_channels_val_sended, num_channels_val);
  EXPECT_EQ(audio_thread_config_global_remix_called, 1);
}

TEST_F(DBusControlTestSuite, SetGlobalOutputChannelRemixInvalid) {
  std::vector<std::pair<int, std::vector<double>>> num_channels_val_sendeds = {
      // num_channels*num_channels != channels_map size
      {6, {0.1, 0.9, 0.9, 0.1}},
      // num_channels > CRAS_CH_MAX
      {CRAS_CH_MAX + 1,
       std::vector<double>((CRAS_CH_MAX + 1) * (CRAS_CH_MAX + 1), 0.5)},
      // num_channels == 0
      {0, {}},
      // num_channels < 0
      {-2, {0.1, 0.9, 0.9, 0.1}}};
  for (auto& [num_channels, channels_map] : num_channels_val_sendeds) {
    CreateMessageCall(CRAS_ROOT_OBJECT_PATH, CRAS_CONTROL_INTERFACE,
                      "SetGlobalOutputChannelRemix")
        .WithInt32(num_channels)
        .WithArrayOfDouble(channels_map)
        .Send();
    WaitForMatches();
    EXPECT_EQ(audio_thread_config_global_remix_called, 0);
  }
}

}  // namespace

extern "C" {
struct main_thread_event_log* main_log;
void cras_system_set_volume(size_t volume) {
  return;
}
int cras_iodev_list_set_node_attr(cras_node_id_t id,
                                  enum ionode_attr attr,
                                  int value) {
  return 0;
}
void cras_system_set_mute(int mute) {
  return;
}
void cras_system_set_user_mute(int mute) {
  return;
}
void cras_system_set_suspended(int suspended) {
  return;
}
void cras_system_set_capture_mute(int mute) {
  return;
}
size_t cras_system_get_volume() {
  return 0;
}
int cras_system_get_system_mute() {
  return 0;
}
int cras_system_get_user_mute() {
  return 0;
}
int cras_system_get_capture_mute() {
  return 0;
}
int cras_system_get_default_output_buffer_size() {
  return 0;
}
int cras_system_get_aec_supported() {
  return 0;
}
int cras_system_get_ns_supported() {
  return 0;
}
int cras_system_get_agc_supported() {
  return 0;
}
bool cras_system_get_deprioritize_bt_wbs_mic() {
  return true;
}
bool cras_rtc_is_running() {
  return true;
}
void cras_iodev_list_select_node(enum CRAS_STREAM_DIRECTION direction,
                                 cras_node_id_t node_id) {
  return;
}
void cras_iodev_list_add_active_node(enum CRAS_STREAM_DIRECTION dir,
                                     cras_node_id_t node_id) {
  return;
}
int cras_system_get_aec_group_id() {
  return 0;
}
void cras_iodev_list_rm_active_node(enum CRAS_STREAM_DIRECTION direction,
                                    cras_node_id_t node_id) {
  return;
}
void cras_system_set_bt_fix_a2dp_packet_size_enabled(bool enabled) {
  return;
}
unsigned cras_system_state_get_active_streams() {
  return 0;
}
unsigned cras_system_state_get_active_streams_by_direction(
    enum CRAS_STREAM_DIRECTION direction) {
  return 0;
}
void cras_system_state_get_input_streams_with_permission(
    uint32_t num_input_streams[CRAS_NUM_CLIENT_TYPE]) {
  return;
}
struct audio_thread* cras_iodev_list_get_audio_thread() {
  return NULL;
}
int audio_thread_config_global_remix(struct audio_thread* thread,
                                     unsigned int num_channels) {
  audio_thread_config_global_remix_called++;
  num_channels_val = num_channels;
  return 0;
}
int cras_iodev_list_set_hotword_model(cras_node_id_t id,
                                      const char* model_name) {
  return 0;
}
int cras_system_state_get_non_empty_status() {
  return 0;
}
int cras_floss_set_enabled(bool enable) {
  return 0;
}
void cras_system_set_bt_wbs_enabled(bool enabled) {
  return;
}
int cras_system_set_noise_cancellation_enabled(bool enable) {
  return 0;
}
bool cras_system_get_noise_cancellation_supported() {
  return true;
}
void cras_system_set_bypass_block_noise_cancellation(bool bypass) {
  return;
}
void cras_system_set_force_sr_bt_enabled(bool enabled) {
  return;
}
bool cras_system_get_force_sr_bt_enabled() {
  return true;
}
int cras_bt_player_update_playback_status(DBusConnection* conn,
                                          const char* status) {
  return 0;
}
int cras_bt_player_update_identity(DBusConnection* conn, const char* identity) {
  return 0;
}
int cras_bt_player_update_position(DBusConnection* conn,
                                   const dbus_int64_t position) {
  return 0;
}
int cras_bt_player_update_metadata(DBusConnection* conn,
                                   const char* title,
                                   const char* artist,
                                   const char* album) {
  return 0;
}
void cras_system_state_set_speak_on_mute_detection(bool enabled) {
  return;
}
bool cras_system_state_get_speak_on_mute_detection_enabled() {
  return true;
}
int cras_system_state_get_output_devs(const struct cras_iodev_info** devs) {
  return 0;
}
int cras_system_state_get_output_nodes(const struct cras_ionode_info** nodes) {
  return 0;
}
int cras_system_state_get_input_devs(const struct cras_iodev_info** devs) {
  return 0;
}
int cras_system_state_get_input_nodes(const struct cras_ionode_info** nodes) {
  return 0;
}
bool cras_system_state_internal_cards_detected() {
  return false;
}
int cras_system_state_num_non_chrome_output_streams() {
  return 0;
}
int audio_thread_dump_thread_info(struct audio_thread* thread,
                                  struct audio_debug_info* info) {
  return 0;
}
int is_utf8_string(const char* string) {
  return 0;
}

bool cras_iodev_is_node_type_internal_mic(const char* type) {
  return true;
}
long convert_input_node_gain_from_dBFS(long dBFS, bool is_internal_mic) {
  return 0;
}
char* cras_iodev_list_get_hotword_models(cras_node_id_t node_id) {
  return NULL;
}
struct cras_observer_client* cras_observer_add(
    const struct cras_observer_ops* ops,
    void* context) {
  return NULL;
}
void cras_observer_remove(struct cras_observer_client* client) {
  return;
}
void cras_system_set_force_respect_ui_gains_enabled(bool enabled) {
  return;
}
}  // extern "C"
