// Copyright 2016 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <gtest/gtest.h>
#include <map>
#include <stdio.h>
#include <syslog.h>
#include <vector>

#include "cras/common/rust_common.h"
#include "cras/src/server/cras_observer.h"
#include "cras_types.h"

extern "C" {
#include "cras/src/server/cras_observer.c"
}

namespace {

static const size_t kNumAlert = 29;

static size_t cras_alert_destroy_called;
static size_t cras_alert_create_called;
static std::vector<struct cras_alert*> cras_alert_create_return_values;
typedef std::map<struct cras_alert*, void*> alert_callback_map;
static alert_callback_map cras_alert_create_prepare_map;
static alert_callback_map cras_alert_add_callback_map;
typedef std::map<struct cras_alert*, unsigned int> alert_flags_map;
static alert_flags_map cras_alert_create_flags_map;
static struct cras_alert* cras_alert_pending_alert_value;
static void* cras_alert_pending_data_value = NULL;
static size_t cras_alert_pending_data_size_value;
static size_t cras_iodev_list_update_device_list_called;
static std::vector<void*> cb_context;
static size_t cb_output_volume_changed_called;
static std::vector<int32_t> cb_output_volume_changed_volume;
static size_t cb_output_mute_changed_called;
static std::vector<int> cb_output_mute_changed_muted;
static std::vector<int> cb_output_mute_changed_user_muted;
static std::vector<int> cb_output_mute_changed_mute_locked;
static size_t cb_capture_gain_changed_called;
static std::vector<int32_t> cb_capture_gain_changed_gain;
static size_t cb_capture_mute_changed_called;
static std::vector<int> cb_capture_mute_changed_muted;
static std::vector<int> cb_capture_mute_changed_mute_locked;
static size_t cb_nodes_changed_called;
static size_t cb_active_node_changed_called;
static std::vector<enum CRAS_STREAM_DIRECTION> cb_active_node_changed_dir;
static std::vector<cras_node_id_t> cb_active_node_changed_node_id;
static size_t cb_output_node_volume_changed_called;
static std::vector<cras_node_id_t> cb_output_node_volume_changed_node_id;
static std::vector<int32_t> cb_output_node_volume_changed_volume;
static size_t cb_node_left_right_swapped_changed_called;
static std::vector<cras_node_id_t> cb_node_left_right_swapped_changed_node_id;
static std::vector<int> cb_node_left_right_swapped_changed_swapped;
static size_t cb_input_node_gain_changed_called;
static std::vector<cras_node_id_t> cb_input_node_gain_changed_node_id;
static std::vector<int32_t> cb_input_node_gain_changed_gain;
static size_t cb_num_active_streams_changed_called;
static std::vector<enum CRAS_STREAM_DIRECTION>
    cb_num_active_streams_changed_dir;
static std::vector<uint32_t> cb_num_active_streams_changed_num;
static size_t cb_num_input_streams_with_permission_called;
static std::vector<std::vector<uint32_t>>
    cb_num_input_streams_with_permission_array;
static size_t cb_severe_underrun_called;
static size_t cb_underrun_called;
static size_t cb_speak_on_mute_detected_called;
static size_t cb_num_non_chrome_output_streams_called;
static std::vector<uint32_t> cb_num_non_chrome_output_streams_values;
static size_t cb_num_stream_ignore_ui_gains_changed_called;
static size_t cb_num_arc_streams_called;
static std::vector<uint32_t> cb_num_arc_streams_values;
static size_t cb_ewma_power_reported_called;
static std::vector<double> cb_ewma_power_reported_values;
static size_t cb_sidetone_supported_changed_called;
static std::vector<bool> cb_sidetone_supported_changed_values;
static size_t cb_audio_effect_ui_appearance_changed_called;
static std::vector<struct CrasEffectUIAppearance>
    cb_audio_effect_ui_appearance_changed_values;

static void ResetStubData() {
  cras_alert_destroy_called = 0;
  cras_alert_create_called = 0;
  cras_alert_create_return_values.clear();
  cras_alert_create_prepare_map.clear();
  cras_alert_create_flags_map.clear();
  cras_alert_add_callback_map.clear();
  cras_alert_pending_alert_value = NULL;
  cras_alert_pending_data_size_value = 0;
  if (cras_alert_pending_data_value) {
    free(cras_alert_pending_data_value);
    cras_alert_pending_data_value = NULL;
  }
  cras_iodev_list_update_device_list_called = 0;
  cb_context.clear();
  cb_output_volume_changed_called = 0;
  cb_output_volume_changed_volume.clear();
  cb_output_mute_changed_called = 0;
  cb_output_mute_changed_muted.clear();
  cb_output_mute_changed_user_muted.clear();
  cb_output_mute_changed_mute_locked.clear();
  cb_capture_gain_changed_called = 0;
  cb_capture_gain_changed_gain.clear();
  cb_capture_mute_changed_called = 0;
  cb_capture_mute_changed_muted.clear();
  cb_capture_mute_changed_mute_locked.clear();
  cb_nodes_changed_called = 0;
  cb_active_node_changed_called = 0;
  cb_active_node_changed_dir.clear();
  cb_active_node_changed_node_id.clear();
  cb_output_node_volume_changed_called = 0;
  cb_output_node_volume_changed_node_id.clear();
  cb_output_node_volume_changed_volume.clear();
  cb_node_left_right_swapped_changed_called = 0;
  cb_node_left_right_swapped_changed_node_id.clear();
  cb_node_left_right_swapped_changed_swapped.clear();
  cb_input_node_gain_changed_called = 0;
  cb_input_node_gain_changed_node_id.clear();
  cb_input_node_gain_changed_gain.clear();
  cb_num_active_streams_changed_called = 0;
  cb_num_active_streams_changed_dir.clear();
  cb_num_active_streams_changed_num.clear();
  cb_num_input_streams_with_permission_called = 0;
  cb_num_input_streams_with_permission_array.clear();
  cb_severe_underrun_called = 0;
  cb_underrun_called = 0;
  cb_speak_on_mute_detected_called = 0;
  cb_num_non_chrome_output_streams_called = 0;
  cb_num_non_chrome_output_streams_values.clear();
  cb_num_stream_ignore_ui_gains_changed_called = 0;
  cb_num_arc_streams_called = 0;
  cb_num_arc_streams_values.clear();
  cb_ewma_power_reported_called = 0;
  cb_ewma_power_reported_values.clear();
  cb_sidetone_supported_changed_called = 0;
  cb_sidetone_supported_changed_values.clear();
  cb_audio_effect_ui_appearance_changed_called = 0;
  cb_audio_effect_ui_appearance_changed_values.clear();
}

// System output volume changed.
void cb_output_volume_changed(void* context, int32_t volume) {
  cb_output_volume_changed_called++;
  cb_context.push_back(context);
  cb_output_volume_changed_volume.push_back(volume);
}
// System output mute changed.
void cb_output_mute_changed(void* context,
                            int muted,
                            int user_muted,
                            int mute_locked) {
  cb_output_mute_changed_called++;
  cb_context.push_back(context);
  cb_output_mute_changed_muted.push_back(muted);
  cb_output_mute_changed_user_muted.push_back(user_muted);
  cb_output_mute_changed_mute_locked.push_back(mute_locked);
}
// System input/capture gain changed.
void cb_capture_gain_changed(void* context, int32_t gain) {
  cb_capture_gain_changed_called++;
  cb_context.push_back(context);
  cb_capture_gain_changed_gain.push_back(gain);
}

// System input/capture mute changed.
void cb_capture_mute_changed(void* context, int muted, int mute_locked) {
  cb_capture_mute_changed_called++;
  cb_context.push_back(context);
  cb_capture_mute_changed_muted.push_back(muted);
  cb_capture_mute_changed_mute_locked.push_back(mute_locked);
}

// Device or node topology changed.
void cb_nodes_changed(void* context) {
  cb_nodes_changed_called++;
  cb_context.push_back(context);
}

/* Active node changed. A notification is sent for every change.
 * When there is no active node, node_id is 0. */
void cb_active_node_changed(void* context,
                            enum CRAS_STREAM_DIRECTION dir,
                            cras_node_id_t node_id) {
  cb_active_node_changed_called++;
  cb_context.push_back(context);
  cb_active_node_changed_dir.push_back(dir);
  cb_active_node_changed_node_id.push_back(node_id);
}

// Output node volume changed.
void cb_output_node_volume_changed(void* context,
                                   cras_node_id_t node_id,
                                   int32_t volume) {
  cb_output_node_volume_changed_called++;
  cb_context.push_back(context);
  cb_output_node_volume_changed_node_id.push_back(node_id);
  cb_output_node_volume_changed_volume.push_back(volume);
}

// Node left/right swapped state change.
void cb_node_left_right_swapped_changed(void* context,
                                        cras_node_id_t node_id,
                                        int swapped) {
  cb_node_left_right_swapped_changed_called++;
  cb_context.push_back(context);
  cb_node_left_right_swapped_changed_node_id.push_back(node_id);
  cb_node_left_right_swapped_changed_swapped.push_back(swapped);
}

// Input gain changed.
void cb_input_node_gain_changed(void* context,
                                cras_node_id_t node_id,
                                int32_t gain) {
  cb_input_node_gain_changed_called++;
  cb_context.push_back(context);
  cb_input_node_gain_changed_node_id.push_back(node_id);
  cb_input_node_gain_changed_gain.push_back(gain);
}

// Number of active streams changed.
void cb_num_active_streams_changed(void* context,
                                   enum CRAS_STREAM_DIRECTION dir,
                                   uint32_t num_active_streams) {
  cb_num_active_streams_changed_called++;
  cb_context.push_back(context);
  cb_num_active_streams_changed_dir.push_back(dir);
  cb_num_active_streams_changed_num.push_back(num_active_streams);
}

void cb_num_input_streams_with_permission_changed(
    void* context,
    uint32_t num_input_streams[CRAS_NUM_CLIENT_TYPE]) {
  cb_num_input_streams_with_permission_called++;
  cb_context.push_back(context);
  cb_num_input_streams_with_permission_array.push_back(std::vector<uint32_t>(
      num_input_streams, num_input_streams + CRAS_NUM_CLIENT_TYPE));
}

void cb_severe_underrun(void* context) {
  cb_severe_underrun_called++;
  cb_context.push_back(context);
}

void cb_underrun(void* context) {
  cb_underrun_called++;
  cb_context.push_back(context);
}

void cb_speak_on_mute_detected(void* context) {
  cb_speak_on_mute_detected_called++;
  cb_context.push_back(context);
}

void cb_num_non_chrome_output_streams(void* context,
                                      uint32_t num_non_chrome_output_streams) {
  cb_num_non_chrome_output_streams_called++;
  cb_num_non_chrome_output_streams_values.push_back(
      num_non_chrome_output_streams);
  cb_context.push_back(context);
}

void cb_num_stream_ignore_ui_gains_changed(void* context, int num) {
  cb_num_stream_ignore_ui_gains_changed_called++;
  cb_context.push_back(context);
}

void cb_num_arc_streams(void* context, uint32_t num_arc_streams) {
  cb_num_arc_streams_called++;
  cb_num_arc_streams_values.push_back(num_arc_streams);
  cb_context.push_back(context);
}

void cb_ewma_power_reported(void* context, double power) {
  cb_ewma_power_reported_called++;
  cb_ewma_power_reported_values.push_back(power);
  cb_context.push_back(context);
}

void cb_sidetone_supported_changed(void* context, bool supported) {
  cb_sidetone_supported_changed_called++;
  cb_sidetone_supported_changed_values.push_back(supported);
  cb_context.push_back(context);
}

void cb_audio_effect_ui_appearance_changed(
    void* context,
    struct CrasEffectUIAppearance appearance) {
  cb_audio_effect_ui_appearance_changed_called++;
  cb_audio_effect_ui_appearance_changed_values.push_back(appearance);
  cb_context.push_back(context);
}

class ObserverTest : public testing::Test {
 protected:
  virtual void SetUp() {
    int rc;

    ResetStubData();
    rc = cras_observer_server_init();
    ASSERT_EQ(0, rc);
    EXPECT_EQ(kNumAlert, cras_alert_create_called);
    EXPECT_EQ(reinterpret_cast<void*>(output_volume_alert),
              cras_alert_add_callback_map[g_observer->alerts.output_volume]);
    EXPECT_EQ(reinterpret_cast<void*>(output_mute_alert),
              cras_alert_add_callback_map[g_observer->alerts.output_mute]);
    EXPECT_EQ(reinterpret_cast<void*>(capture_gain_alert),
              cras_alert_add_callback_map[g_observer->alerts.capture_gain]);
    EXPECT_EQ(reinterpret_cast<void*>(capture_mute_alert),
              cras_alert_add_callback_map[g_observer->alerts.capture_mute]);
    EXPECT_EQ(reinterpret_cast<void*>(nodes_alert),
              cras_alert_add_callback_map[g_observer->alerts.nodes]);
    EXPECT_EQ(reinterpret_cast<void*>(nodes_prepare),
              cras_alert_create_prepare_map[g_observer->alerts.nodes]);
    EXPECT_EQ(reinterpret_cast<void*>(active_node_alert),
              cras_alert_add_callback_map[g_observer->alerts.active_node]);
    EXPECT_EQ(CRAS_ALERT_FLAG_KEEP_ALL_DATA,
              cras_alert_create_flags_map[g_observer->alerts.active_node]);
    EXPECT_EQ(
        reinterpret_cast<void*>(output_node_volume_alert),
        cras_alert_add_callback_map[g_observer->alerts.output_node_volume]);
    EXPECT_EQ(reinterpret_cast<void*>(node_left_right_swapped_alert),
              cras_alert_add_callback_map[g_observer->alerts
                                              .node_left_right_swapped]);
    EXPECT_EQ(reinterpret_cast<void*>(input_node_gain_alert),
              cras_alert_add_callback_map[g_observer->alerts.input_node_gain]);
    EXPECT_EQ(reinterpret_cast<void*>(num_active_streams_alert),
              cras_alert_add_callback_map
                  [g_observer->alerts.num_active_streams[CRAS_STREAM_OUTPUT]]);
    EXPECT_EQ(reinterpret_cast<void*>(num_active_streams_alert),
              cras_alert_add_callback_map
                  [g_observer->alerts.num_active_streams[CRAS_STREAM_INPUT]]);
    EXPECT_EQ(reinterpret_cast<void*>(num_active_streams_alert),
              cras_alert_add_callback_map[g_observer->alerts.num_active_streams
                                              [CRAS_STREAM_POST_MIX_PRE_DSP]]);
    EXPECT_EQ(reinterpret_cast<void*>(suspend_changed_alert),
              cras_alert_add_callback_map[g_observer->alerts.suspend_changed]);
    EXPECT_EQ(
        reinterpret_cast<void*>(hotword_triggered_alert),
        cras_alert_add_callback_map[g_observer->alerts.hotword_triggered]);
    EXPECT_EQ(reinterpret_cast<void*>(non_empty_audio_state_changed_alert),
              cras_alert_add_callback_map[g_observer->alerts
                                              .non_empty_audio_state_changed]);
    EXPECT_EQ(
        reinterpret_cast<void*>(bt_battery_changed_alert),
        cras_alert_add_callback_map[g_observer->alerts.bt_battery_changed]);
    EXPECT_EQ(reinterpret_cast<void*>(severe_underrun_alert),
              cras_alert_add_callback_map[g_observer->alerts.severe_underrun]);
    EXPECT_EQ(reinterpret_cast<void*>(underrun_alert),
              cras_alert_add_callback_map[g_observer->alerts.underrun]);

    cras_observer_get_ops(NULL, &ops1_);
    EXPECT_NE(0, cras_observer_ops_are_empty(&ops1_));

    cras_observer_get_ops(NULL, &ops2_);
    EXPECT_NE(0, cras_observer_ops_are_empty(&ops2_));

    context1_ = reinterpret_cast<void*>(1);
    context2_ = reinterpret_cast<void*>(2);
  }

  virtual void TearDown() {
    cras_observer_server_free();
    EXPECT_EQ(kNumAlert, cras_alert_destroy_called);
    ResetStubData();
  }

  void DoObserverAlert(cras_alert_cb alert, void* data) {
    client1_ = cras_observer_add(&ops1_, context1_);
    client2_ = cras_observer_add(&ops2_, context2_);
    ASSERT_NE(client1_, reinterpret_cast<struct cras_observer_client*>(NULL));
    ASSERT_NE(client2_, reinterpret_cast<struct cras_observer_client*>(NULL));

    ASSERT_NE(alert, reinterpret_cast<cras_alert_cb>(NULL));
    alert(NULL, data);

    EXPECT_EQ(cb_context.size(), 2);

    EXPECT_EQ(cb_context[0], context1_);
    EXPECT_EQ(cb_context[1], context2_);
  }

  void DoObserverRemoveClear(cras_alert_cb alert, void* data) {
    ASSERT_NE(alert, reinterpret_cast<cras_alert_cb>(NULL));
    ASSERT_NE(client1_, reinterpret_cast<struct cras_observer_client*>(NULL));
    ASSERT_NE(client2_, reinterpret_cast<struct cras_observer_client*>(NULL));

    // Test observer removal.
    cras_observer_remove(client1_);
    cb_context.clear();
    alert(NULL, data);
    EXPECT_EQ(cb_context[0], context2_);
    EXPECT_EQ(cb_context.size(), 1);

    // Clear out ops1_.
    cras_observer_get_ops(NULL, &ops1_);
    EXPECT_NE(0, cras_observer_ops_are_empty(&ops1_));

    // Get the current value of ops2_ into ops1_.
    cras_observer_get_ops(client2_, &ops1_);
    EXPECT_EQ(0, memcmp((void*)&ops1_, (void*)&ops2_, sizeof(ops1_)));

    // Clear out opts for client2.
    cras_observer_get_ops(NULL, &ops2_);
    EXPECT_NE(0, cras_observer_ops_are_empty(&ops2_));
    cras_observer_set_ops(client2_, &ops2_);

    cras_observer_remove(client2_);
    cb_context.clear();
    alert(NULL, data);
    // No callbacks executed.
    EXPECT_EQ(cb_context.size(), 0);
  }

  struct cras_observer_client* client1_;
  struct cras_observer_client* client2_;
  struct cras_observer_ops ops1_;
  struct cras_observer_ops ops2_;
  void* context1_;
  void* context2_;
};

TEST_F(ObserverTest, NotifyOutputVolume) {
  struct cras_observer_alert_data_volume* data;
  const int32_t volume = 100;

  cras_observer_notify_output_volume(volume);
  EXPECT_EQ(cras_alert_pending_alert_value, g_observer->alerts.output_volume);
  ASSERT_EQ(cras_alert_pending_data_size_value, sizeof(*data));
  ASSERT_NE(cras_alert_pending_data_value, reinterpret_cast<void*>(NULL));
  data = reinterpret_cast<struct cras_observer_alert_data_volume*>(
      cras_alert_pending_data_value);
  EXPECT_EQ(data->volume, volume);

  ops1_.output_volume_changed = cb_output_volume_changed;
  ops2_.output_volume_changed = cb_output_volume_changed;
  DoObserverAlert(output_volume_alert, data);
  ASSERT_EQ(2, cb_output_volume_changed_called);
  EXPECT_EQ(cb_output_volume_changed_volume[0], volume);
  EXPECT_EQ(cb_output_volume_changed_volume[1], volume);

  DoObserverRemoveClear(output_volume_alert, data);
};

TEST_F(ObserverTest, NotifyOutputMute) {
  struct cras_observer_alert_data_mute* data;
  const int muted = 1;
  const int user_muted = 0;
  const int mute_locked = 0;

  cras_observer_notify_output_mute(muted, user_muted, mute_locked);
  EXPECT_EQ(cras_alert_pending_alert_value, g_observer->alerts.output_mute);
  ASSERT_EQ(cras_alert_pending_data_size_value, sizeof(*data));
  ASSERT_NE(cras_alert_pending_data_value, reinterpret_cast<void*>(NULL));
  data = reinterpret_cast<struct cras_observer_alert_data_mute*>(
      cras_alert_pending_data_value);
  EXPECT_EQ(data->muted, muted);
  EXPECT_EQ(data->user_muted, user_muted);
  EXPECT_EQ(data->mute_locked, mute_locked);

  ops1_.output_mute_changed = cb_output_mute_changed;
  ops2_.output_mute_changed = cb_output_mute_changed;
  DoObserverAlert(output_mute_alert, data);
  ASSERT_EQ(2, cb_output_mute_changed_called);
  EXPECT_EQ(cb_output_mute_changed_muted[0], muted);
  EXPECT_EQ(cb_output_mute_changed_muted[1], muted);
  EXPECT_EQ(cb_output_mute_changed_user_muted[0], user_muted);
  EXPECT_EQ(cb_output_mute_changed_user_muted[1], user_muted);
  EXPECT_EQ(cb_output_mute_changed_mute_locked[0], mute_locked);
  EXPECT_EQ(cb_output_mute_changed_mute_locked[1], mute_locked);

  DoObserverRemoveClear(output_mute_alert, data);
};

TEST_F(ObserverTest, NotifyCaptureGain) {
  struct cras_observer_alert_data_volume* data;
  const int32_t gain = -20;

  cras_observer_notify_capture_gain(gain);
  EXPECT_EQ(cras_alert_pending_alert_value, g_observer->alerts.capture_gain);
  ASSERT_EQ(cras_alert_pending_data_size_value, sizeof(*data));
  ASSERT_NE(cras_alert_pending_data_value, reinterpret_cast<void*>(NULL));
  data = reinterpret_cast<struct cras_observer_alert_data_volume*>(
      cras_alert_pending_data_value);
  EXPECT_EQ(data->volume, gain);

  ops1_.capture_gain_changed = cb_capture_gain_changed;
  ops2_.capture_gain_changed = cb_capture_gain_changed;
  DoObserverAlert(capture_gain_alert, data);
  ASSERT_EQ(2, cb_capture_gain_changed_called);
  EXPECT_EQ(cb_capture_gain_changed_gain[0], gain);
  EXPECT_EQ(cb_capture_gain_changed_gain[1], gain);

  DoObserverRemoveClear(capture_gain_alert, data);
};

TEST_F(ObserverTest, NotifyCaptureMute) {
  struct cras_observer_alert_data_mute* data;
  const int muted = 1;
  const int mute_locked = 0;

  cras_observer_notify_capture_mute(muted, mute_locked);
  EXPECT_EQ(cras_alert_pending_alert_value, g_observer->alerts.capture_mute);
  ASSERT_EQ(cras_alert_pending_data_size_value, sizeof(*data));
  ASSERT_NE(cras_alert_pending_data_value, reinterpret_cast<void*>(NULL));
  data = reinterpret_cast<struct cras_observer_alert_data_mute*>(
      cras_alert_pending_data_value);
  EXPECT_EQ(data->muted, muted);
  EXPECT_EQ(data->mute_locked, mute_locked);

  ops1_.capture_mute_changed = cb_capture_mute_changed;
  ops2_.capture_mute_changed = cb_capture_mute_changed;
  DoObserverAlert(capture_mute_alert, data);
  ASSERT_EQ(2, cb_capture_mute_changed_called);
  EXPECT_EQ(cb_capture_mute_changed_muted[0], muted);
  EXPECT_EQ(cb_capture_mute_changed_muted[1], muted);
  EXPECT_EQ(cb_capture_mute_changed_mute_locked[0], mute_locked);
  EXPECT_EQ(cb_capture_mute_changed_mute_locked[1], mute_locked);

  DoObserverRemoveClear(capture_mute_alert, data);
};

TEST_F(ObserverTest, NotifyNodes) {
  cras_observer_notify_nodes();
  EXPECT_EQ(cras_alert_pending_alert_value, g_observer->alerts.nodes);

  ops1_.nodes_changed = cb_nodes_changed;
  ops2_.nodes_changed = cb_nodes_changed;
  DoObserverAlert(nodes_alert, NULL);
  ASSERT_EQ(2, cb_nodes_changed_called);

  DoObserverRemoveClear(nodes_alert, NULL);
};

TEST_F(ObserverTest, NotifyActiveNode) {
  struct cras_observer_alert_data_active_node* data;
  const enum CRAS_STREAM_DIRECTION dir = CRAS_STREAM_INPUT;
  const cras_node_id_t node_id = 0x0001000100020002;

  cras_observer_notify_active_node(dir, node_id);
  EXPECT_EQ(cras_alert_pending_alert_value, g_observer->alerts.active_node);
  ASSERT_EQ(cras_alert_pending_data_size_value, sizeof(*data));
  ASSERT_NE(cras_alert_pending_data_value, reinterpret_cast<void*>(NULL));
  data = reinterpret_cast<struct cras_observer_alert_data_active_node*>(
      cras_alert_pending_data_value);
  EXPECT_EQ(data->node_id, node_id);
  EXPECT_EQ(data->direction, dir);

  ops1_.active_node_changed = cb_active_node_changed;
  ops2_.active_node_changed = cb_active_node_changed;
  DoObserverAlert(active_node_alert, data);
  ASSERT_EQ(2, cb_active_node_changed_called);
  EXPECT_EQ(cb_active_node_changed_dir[0], dir);
  EXPECT_EQ(cb_active_node_changed_dir[1], dir);
  EXPECT_EQ(cb_active_node_changed_node_id[0], node_id);
  EXPECT_EQ(cb_active_node_changed_node_id[1], node_id);

  DoObserverRemoveClear(active_node_alert, data);
};

TEST_F(ObserverTest, NotifyOutputNodeVolume) {
  struct cras_observer_alert_data_node_volume* data;
  const cras_node_id_t node_id = 0x0001000100020002;
  const int32_t volume = 100;

  cras_observer_notify_output_node_volume(node_id, volume);
  EXPECT_EQ(cras_alert_pending_alert_value,
            g_observer->alerts.output_node_volume);
  ASSERT_EQ(cras_alert_pending_data_size_value, sizeof(*data));
  ASSERT_NE(cras_alert_pending_data_value, reinterpret_cast<void*>(NULL));
  data = reinterpret_cast<struct cras_observer_alert_data_node_volume*>(
      cras_alert_pending_data_value);
  EXPECT_EQ(data->node_id, node_id);
  EXPECT_EQ(data->volume, volume);

  ops1_.output_node_volume_changed = cb_output_node_volume_changed;
  ops2_.output_node_volume_changed = cb_output_node_volume_changed;
  DoObserverAlert(output_node_volume_alert, data);
  ASSERT_EQ(2, cb_output_node_volume_changed_called);
  EXPECT_EQ(cb_output_node_volume_changed_volume[0], volume);
  EXPECT_EQ(cb_output_node_volume_changed_volume[1], volume);
  EXPECT_EQ(cb_output_node_volume_changed_node_id[0], node_id);
  EXPECT_EQ(cb_output_node_volume_changed_node_id[1], node_id);

  DoObserverRemoveClear(output_node_volume_alert, data);
};

TEST_F(ObserverTest, NotifyNodeLeftRightSwapped) {
  struct cras_observer_alert_data_node_lr_swapped* data;
  const cras_node_id_t node_id = 0x0001000100020002;
  const int swapped = 1;

  cras_observer_notify_node_left_right_swapped(node_id, swapped);
  EXPECT_EQ(cras_alert_pending_alert_value,
            g_observer->alerts.node_left_right_swapped);
  ASSERT_EQ(cras_alert_pending_data_size_value, sizeof(*data));
  ASSERT_NE(cras_alert_pending_data_value, reinterpret_cast<void*>(NULL));
  data = reinterpret_cast<struct cras_observer_alert_data_node_lr_swapped*>(
      cras_alert_pending_data_value);
  EXPECT_EQ(data->node_id, node_id);
  EXPECT_EQ(data->swapped, swapped);

  ops1_.node_left_right_swapped_changed = cb_node_left_right_swapped_changed;
  ops2_.node_left_right_swapped_changed = cb_node_left_right_swapped_changed;
  DoObserverAlert(node_left_right_swapped_alert, data);
  ASSERT_EQ(2, cb_node_left_right_swapped_changed_called);
  EXPECT_EQ(cb_node_left_right_swapped_changed_swapped[0], swapped);
  EXPECT_EQ(cb_node_left_right_swapped_changed_swapped[1], swapped);
  EXPECT_EQ(cb_node_left_right_swapped_changed_node_id[0], node_id);
  EXPECT_EQ(cb_node_left_right_swapped_changed_node_id[1], node_id);

  DoObserverRemoveClear(node_left_right_swapped_alert, data);
};

TEST_F(ObserverTest, NotifyInputNodeGain) {
  struct cras_observer_alert_data_node_volume* data;
  const cras_node_id_t node_id = 0x0001000100020002;
  const int32_t gain = -20;

  cras_observer_notify_input_node_gain(node_id, gain);
  EXPECT_EQ(cras_alert_pending_alert_value, g_observer->alerts.input_node_gain);
  ASSERT_EQ(cras_alert_pending_data_size_value, sizeof(*data));
  ASSERT_NE(cras_alert_pending_data_value, reinterpret_cast<void*>(NULL));
  data = reinterpret_cast<struct cras_observer_alert_data_node_volume*>(
      cras_alert_pending_data_value);
  EXPECT_EQ(data->node_id, node_id);
  EXPECT_EQ(data->volume, gain);

  ops1_.input_node_gain_changed = cb_input_node_gain_changed;
  ops2_.input_node_gain_changed = cb_input_node_gain_changed;
  DoObserverAlert(input_node_gain_alert, data);
  ASSERT_EQ(2, cb_input_node_gain_changed_called);
  EXPECT_EQ(cb_input_node_gain_changed_gain[0], gain);
  EXPECT_EQ(cb_input_node_gain_changed_gain[1], gain);
  EXPECT_EQ(cb_input_node_gain_changed_node_id[0], node_id);
  EXPECT_EQ(cb_input_node_gain_changed_node_id[1], node_id);

  DoObserverRemoveClear(input_node_gain_alert, data);
};

TEST_F(ObserverTest, NotifySuspendChanged) {
  struct cras_observer_alert_data_suspend* data;

  cras_observer_notify_suspend_changed(1);
  EXPECT_EQ(cras_alert_pending_alert_value, g_observer->alerts.suspend_changed);
  ASSERT_EQ(cras_alert_pending_data_size_value, sizeof(*data));
  ASSERT_NE(cras_alert_pending_data_value, reinterpret_cast<void*>(NULL));
  data = reinterpret_cast<struct cras_observer_alert_data_suspend*>(
      cras_alert_pending_data_value);
  EXPECT_EQ(data->suspended, 1);

  cras_observer_notify_suspend_changed(0);
  EXPECT_EQ(cras_alert_pending_alert_value, g_observer->alerts.suspend_changed);
  ASSERT_EQ(cras_alert_pending_data_size_value, sizeof(*data));
  ASSERT_NE(cras_alert_pending_data_value, reinterpret_cast<void*>(NULL));
  data = reinterpret_cast<struct cras_observer_alert_data_suspend*>(
      cras_alert_pending_data_value);
  EXPECT_EQ(data->suspended, 0);
}

TEST_F(ObserverTest, NotifyNumActiveStreams) {
  struct cras_observer_alert_data_streams* data;
  const enum CRAS_STREAM_DIRECTION dir = CRAS_STREAM_INPUT;
  const uint32_t active_streams = 10;

  cras_observer_notify_num_active_streams(dir, active_streams);
  EXPECT_EQ(cras_alert_pending_alert_value,
            g_observer->alerts.num_active_streams[CRAS_STREAM_INPUT]);
  ASSERT_EQ(cras_alert_pending_data_size_value, sizeof(*data));
  ASSERT_NE(cras_alert_pending_data_value, reinterpret_cast<void*>(NULL));
  data = reinterpret_cast<struct cras_observer_alert_data_streams*>(
      cras_alert_pending_data_value);
  EXPECT_EQ(data->num_active_streams, active_streams);
  EXPECT_EQ(data->direction, dir);

  ops1_.num_active_streams_changed = cb_num_active_streams_changed;
  ops2_.num_active_streams_changed = cb_num_active_streams_changed;
  DoObserverAlert(num_active_streams_alert, data);
  ASSERT_EQ(2, cb_num_active_streams_changed_called);
  EXPECT_EQ(cb_num_active_streams_changed_dir[0], dir);
  EXPECT_EQ(cb_num_active_streams_changed_dir[1], dir);
  EXPECT_EQ(cb_num_active_streams_changed_num[0], active_streams);
  EXPECT_EQ(cb_num_active_streams_changed_num[1], active_streams);

  DoObserverRemoveClear(num_active_streams_alert, data);
};

TEST_F(ObserverTest, NotifyNumInputStreamsWithPermission) {
  struct cras_observer_alert_data_input_streams* data;
  uint32_t num_input_streams[CRAS_NUM_CLIENT_TYPE] = {};
  for (unsigned type = 0; type < CRAS_NUM_CLIENT_TYPE; ++type) {
    num_input_streams[type] = (uint32_t)type;
  }

  cras_observer_notify_input_streams_with_permission(num_input_streams);
  ASSERT_EQ(cras_alert_pending_data_size_value, sizeof(*data));
  ASSERT_NE(cras_alert_pending_data_value, reinterpret_cast<void*>(NULL));
  data = reinterpret_cast<struct cras_observer_alert_data_input_streams*>(
      cras_alert_pending_data_value);
  for (unsigned type = 0; type < CRAS_NUM_CLIENT_TYPE; ++type) {
    EXPECT_EQ(data->num_input_streams[type], num_input_streams[type]);
  }

  ops1_.num_input_streams_with_permission_changed =
      cb_num_input_streams_with_permission_changed;
  ops2_.num_input_streams_with_permission_changed =
      cb_num_input_streams_with_permission_changed;
  DoObserverAlert(num_input_streams_with_permission_alert, data);
  ASSERT_EQ(2, cb_num_input_streams_with_permission_called);
  for (auto cb_num_input_streams : cb_num_input_streams_with_permission_array) {
    ASSERT_EQ(cb_num_input_streams.size(), (size_t)CRAS_NUM_CLIENT_TYPE);
    for (unsigned type = 0; type < CRAS_NUM_CLIENT_TYPE; ++type) {
      EXPECT_EQ(cb_num_input_streams[type], num_input_streams[type]);
    }
  }
  DoObserverRemoveClear(num_input_streams_with_permission_alert, data);
}

TEST_F(ObserverTest, NotifyHotwordTriggered) {
  struct cras_observer_alert_data_hotword_triggered* data;

  cras_observer_notify_hotword_triggered(100, 200);
  EXPECT_EQ(cras_alert_pending_alert_value,
            g_observer->alerts.hotword_triggered);
  ASSERT_EQ(cras_alert_pending_data_size_value, sizeof(*data));
  ASSERT_NE(cras_alert_pending_data_value, reinterpret_cast<void*>(NULL));
  data = reinterpret_cast<struct cras_observer_alert_data_hotword_triggered*>(
      cras_alert_pending_data_value);
  EXPECT_EQ(data->tv_sec, 100);
  EXPECT_EQ(data->tv_nsec, 200);
}

TEST_F(ObserverTest, NonEmpyAudioStateChanged) {
  struct cras_observer_non_empty_audio_state* data;

  cras_observer_notify_non_empty_audio_state_changed(1);
  EXPECT_EQ(cras_alert_pending_alert_value,
            g_observer->alerts.non_empty_audio_state_changed);
  ASSERT_EQ(cras_alert_pending_data_size_value, sizeof(*data));
  ASSERT_NE(cras_alert_pending_data_value, reinterpret_cast<void*>(NULL));
  data = reinterpret_cast<struct cras_observer_non_empty_audio_state*>(
      cras_alert_pending_data_value);
  EXPECT_EQ(data->non_empty, 1);
}

TEST_F(ObserverTest, BluetoothBatteryChanged) {
  struct cras_observer_alert_data_bt_battery_changed* data;
  const char* address = "test";

  cras_observer_notify_bt_battery_changed(address, 30);
  EXPECT_EQ(cras_alert_pending_alert_value,
            g_observer->alerts.bt_battery_changed);
  ASSERT_EQ(cras_alert_pending_data_size_value, sizeof(*data));
  ASSERT_NE(cras_alert_pending_data_value, reinterpret_cast<void*>(NULL));
  data = reinterpret_cast<struct cras_observer_alert_data_bt_battery_changed*>(
      cras_alert_pending_data_value);
  EXPECT_EQ(data->address, address);
  EXPECT_EQ(data->level, 30);
}

TEST_F(ObserverTest, SevereUnderrun) {
  cras_observer_notify_severe_underrun();
  EXPECT_EQ(cras_alert_pending_alert_value, g_observer->alerts.severe_underrun);

  ops1_.severe_underrun = cb_severe_underrun;
  ops2_.severe_underrun = cb_severe_underrun;
  DoObserverAlert(severe_underrun_alert, NULL);
  ASSERT_EQ(2, cb_severe_underrun_called);

  DoObserverRemoveClear(severe_underrun_alert, NULL);
}

TEST_F(ObserverTest, Underrun) {
  cras_observer_notify_underrun();
  EXPECT_EQ(cras_alert_pending_alert_value, g_observer->alerts.underrun);

  ops1_.underrun = cb_underrun;
  ops2_.underrun = cb_underrun;
  DoObserverAlert(underrun_alert, NULL);
  ASSERT_EQ(2, cb_underrun_called);

  DoObserverRemoveClear(underrun_alert, NULL);
}

TEST_F(ObserverTest, SpeakOnMuteDetected) {
  cras_observer_notify_speak_on_mute_detected();
  EXPECT_EQ(cras_alert_pending_alert_value,
            g_observer->alerts.speak_on_mute_detected);

  ops1_.speak_on_mute_detected = cb_speak_on_mute_detected;
  ops2_.speak_on_mute_detected = cb_speak_on_mute_detected;
  DoObserverAlert(speak_on_mute_detected_alert, NULL);
  ASSERT_EQ(cb_speak_on_mute_detected_called, 2);

  DoObserverRemoveClear(speak_on_mute_detected_alert, NULL);
}

TEST_F(ObserverTest, NumNonChromeOutputStreamsChanged) {
  cras_observer_notify_num_non_chrome_output_streams(99);
  EXPECT_EQ(cras_alert_pending_alert_value,
            g_observer->alerts.num_non_chrome_output_streams);
  auto* data = reinterpret_cast<
      struct cras_observer_alert_data_num_non_chrome_output_streams*>(
      cras_alert_pending_data_value);
  EXPECT_EQ(data->num_non_chrome_output_streams, 99);

  ops1_.num_non_chrome_output_streams_changed =
      cb_num_non_chrome_output_streams;
  ops2_.num_non_chrome_output_streams_changed =
      cb_num_non_chrome_output_streams;
  DoObserverAlert(num_non_chrome_output_streams_alert, data);
  ASSERT_EQ(cb_num_non_chrome_output_streams_called, 2);
  EXPECT_EQ(cb_num_non_chrome_output_streams_values,
            (std::vector<uint32_t>{99, 99}));

  DoObserverRemoveClear(num_non_chrome_output_streams_alert, data);
}

TEST_F(ObserverTest, NumStreamIgnoreUiGainsChanged) {
  cras_observer_notify_num_stream_ignore_ui_gains_changed(1);
  EXPECT_EQ(cras_alert_pending_alert_value,
            g_observer->alerts.num_stream_ignore_ui_gains_changed);
  auto* data =
      reinterpret_cast<struct cras_observer_num_stream_ignore_ui_gains*>(
          cras_alert_pending_data_value);

  ops1_.num_stream_ignore_ui_gains_changed =
      cb_num_stream_ignore_ui_gains_changed;
  ops2_.num_stream_ignore_ui_gains_changed =
      cb_num_stream_ignore_ui_gains_changed;

  DoObserverAlert(num_stream_ignore_ui_gains_changed_alert, data);
  ASSERT_EQ(cb_num_stream_ignore_ui_gains_changed_called, 2);

  DoObserverRemoveClear(num_stream_ignore_ui_gains_changed_alert, data);
}

TEST_F(ObserverTest, NumArcStreamsChanged) {
  cras_observer_notify_num_arc_streams(99);
  EXPECT_EQ(cras_alert_pending_alert_value, g_observer->alerts.num_arc_streams);
  auto* data =
      reinterpret_cast<struct cras_observer_alert_data_num_arc_streams*>(
          cras_alert_pending_data_value);
  EXPECT_EQ(data->num_arc_streams, 99);

  ops1_.num_arc_streams_changed = cb_num_arc_streams;
  ops2_.num_arc_streams_changed = cb_num_arc_streams;

  DoObserverAlert(num_arc_streams_alert, data);
  ASSERT_EQ(cb_num_arc_streams_called, 2);
  EXPECT_EQ(cb_num_arc_streams_values, (std::vector<uint32_t>{99, 99}));

  DoObserverRemoveClear(num_arc_streams_alert, data);
}

TEST_F(ObserverTest, EwmaPowerReported) {
  cras_observer_notify_ewma_power_reported(1.0);
  EXPECT_EQ(cras_alert_pending_alert_value,
            g_observer->alerts.ewma_power_reported);
  auto* data =
      reinterpret_cast<struct cras_observer_alert_data_ewma_power_reported*>(
          cras_alert_pending_data_value);
  EXPECT_EQ(data->power, 1.0);

  ops1_.ewma_power_reported = cb_ewma_power_reported;
  ops2_.ewma_power_reported = cb_ewma_power_reported;

  DoObserverAlert(ewma_power_reported_alert, data);
  ASSERT_EQ(cb_ewma_power_reported_called, 2);
  EXPECT_EQ(cb_ewma_power_reported_values, (std::vector<double>{1.0, 1.0}));

  DoObserverRemoveClear(ewma_power_reported_alert, data);
}

TEST_F(ObserverTest, SidetoneSupportedChanged) {
  cras_observer_notify_sidetone_supported_changed(true);
  EXPECT_EQ(cras_alert_pending_alert_value,
            g_observer->alerts.sidetone_supported_changed);
  auto* data = reinterpret_cast<
      struct cras_observer_alert_data_sidetone_supported_changed*>(
      cras_alert_pending_data_value);
  EXPECT_EQ(data->supported, true);

  ops1_.sidetone_supported_changed = cb_sidetone_supported_changed;
  ops2_.sidetone_supported_changed = cb_sidetone_supported_changed;

  DoObserverAlert(sidetone_supported_changed_alert, data);
  ASSERT_EQ(cb_sidetone_supported_changed_called, 2);
  EXPECT_EQ(cb_sidetone_supported_changed_values,
            (std::vector<bool>{true, true}));

  DoObserverRemoveClear(sidetone_supported_changed_alert, data);
}

TEST_F(ObserverTest, AudioEffectUIAppearanceChanged) {
  struct CrasEffectUIAppearance appearance {
    .toggle_type = EFFECT_TYPE_STYLE_TRANSFER,
    .effect_mode_options = EFFECT_TYPE_STYLE_TRANSFER,
    .show_effect_fallback_message = true
  };
  cras_observer_notify_audio_effect_ui_appearance_changed(appearance);
  EXPECT_EQ(cras_alert_pending_alert_value,
            g_observer->alerts.audio_effect_ui_appearance_changed);
  auto* data = reinterpret_cast<
      struct cras_observer_alert_data_audio_effect_ui_appearance_changed*>(
      cras_alert_pending_data_value);
  EXPECT_EQ(data->ui_appearance.toggle_type, appearance.toggle_type);
  EXPECT_EQ(data->ui_appearance.effect_mode_options,
            appearance.effect_mode_options);
  EXPECT_EQ(data->ui_appearance.show_effect_fallback_message,
            appearance.show_effect_fallback_message);

  ops1_.audio_effect_ui_appearance_changed =
      cb_audio_effect_ui_appearance_changed;
  ops2_.audio_effect_ui_appearance_changed =
      cb_audio_effect_ui_appearance_changed;

  DoObserverAlert(audio_effect_ui_appearance_changed_alert, &appearance);
  ASSERT_EQ(cb_audio_effect_ui_appearance_changed_called, 2);
  EXPECT_EQ(cb_audio_effect_ui_appearance_changed_values.size(), 2);
  EXPECT_EQ(cb_audio_effect_ui_appearance_changed_values[0].toggle_type,
            appearance.toggle_type);
  EXPECT_EQ(cb_audio_effect_ui_appearance_changed_values[1].toggle_type,
            appearance.toggle_type);
  EXPECT_EQ(cb_audio_effect_ui_appearance_changed_values[0].effect_mode_options,
            appearance.effect_mode_options);
  EXPECT_EQ(cb_audio_effect_ui_appearance_changed_values[1].effect_mode_options,
            appearance.effect_mode_options);
  EXPECT_EQ(cb_audio_effect_ui_appearance_changed_values[0]
                .show_effect_fallback_message,
            appearance.show_effect_fallback_message);
  EXPECT_EQ(cb_audio_effect_ui_appearance_changed_values[1]
                .show_effect_fallback_message,
            appearance.show_effect_fallback_message);

  DoObserverRemoveClear(audio_effect_ui_appearance_changed_alert, &appearance);
}

// Stubs
extern "C" {

void cras_alert_destroy(struct cras_alert* alert) {
  cras_alert_destroy_called++;
}

struct cras_alert* cras_alert_create(cras_alert_prepare prepare,
                                     unsigned int flags) {
  struct cras_alert* alert = NULL;

  cras_alert_create_called++;
  alert = reinterpret_cast<struct cras_alert*>(cras_alert_create_called);
  cras_alert_create_return_values.push_back(alert);
  cras_alert_create_flags_map[alert] = flags;
  cras_alert_create_prepare_map[alert] = reinterpret_cast<void*>(prepare);
  return alert;
}

int cras_alert_add_callback(struct cras_alert* alert,
                            cras_alert_cb cb,
                            void* arg) {
  cras_alert_add_callback_map[alert] = reinterpret_cast<void*>(cb);
  return 0;
}

void cras_alert_pending(struct cras_alert* alert) {
  cras_alert_pending_alert_value = alert;
}

void cras_alert_pending_data(struct cras_alert* alert,
                             void* data,
                             size_t data_size) {
  cras_alert_pending_alert_value = alert;
  cras_alert_pending_data_size_value = data_size;
  if (cras_alert_pending_data_value) {
    free(cras_alert_pending_data_value);
  }
  if (data) {
    cras_alert_pending_data_value = malloc(data_size);
    memcpy(cras_alert_pending_data_value, data, data_size);
  } else {
    cras_alert_pending_data_value = NULL;
  }
}

void cras_iodev_list_update_device_list() {
  cras_iodev_list_update_device_list_called++;
}

}  // extern "C"

}  // namespace

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  openlog(NULL, LOG_PERROR, LOG_USER);
  return RUN_ALL_TESTS();
}
