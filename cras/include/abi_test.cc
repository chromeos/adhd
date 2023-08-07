// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cras_types.h"
#include "gtest/gtest.h"

TEST(ABI, CrasServerState) {
  // ARC++ expects that these fields are at these specific offsets.
  // Do not change unless you also uprev ARC++'s CRAS client.
  EXPECT_EQ(0, offsetof(struct cras_server_state, state_version));
  EXPECT_EQ(4, offsetof(struct cras_server_state, volume));
  EXPECT_EQ(8, offsetof(struct cras_server_state, min_volume_dBFS));
  EXPECT_EQ(12, offsetof(struct cras_server_state, max_volume_dBFS));
  EXPECT_EQ(16, offsetof(struct cras_server_state, mute));
  EXPECT_EQ(20, offsetof(struct cras_server_state, user_mute));
  EXPECT_EQ(24, offsetof(struct cras_server_state, mute_locked));
  EXPECT_EQ(28, offsetof(struct cras_server_state, suspended));
  EXPECT_EQ(32, offsetof(struct cras_server_state, capture_gain));
  EXPECT_EQ(36, offsetof(struct cras_server_state, capture_mute));
  EXPECT_EQ(40, offsetof(struct cras_server_state, capture_mute_locked));
  EXPECT_EQ(44, offsetof(struct cras_server_state, num_streams_attached));
  EXPECT_EQ(48, offsetof(struct cras_server_state, num_output_devs));
  EXPECT_EQ(52, offsetof(struct cras_server_state, num_input_devs));
  EXPECT_EQ(56, offsetof(struct cras_server_state, output_devs));
  EXPECT_EQ(1736, offsetof(struct cras_server_state, input_devs));
  EXPECT_EQ(3416, offsetof(struct cras_server_state, num_output_nodes));
  EXPECT_EQ(3420, offsetof(struct cras_server_state, num_input_nodes));
  EXPECT_EQ(3424, offsetof(struct cras_server_state, output_nodes));
  EXPECT_EQ(7024, offsetof(struct cras_server_state, input_nodes));
  EXPECT_EQ(10624, offsetof(struct cras_server_state, num_attached_clients));
  EXPECT_EQ(10628, offsetof(struct cras_server_state, client_info));
  EXPECT_EQ(10948, offsetof(struct cras_server_state, update_count));
  EXPECT_EQ(10952, offsetof(struct cras_server_state, num_active_streams));
  EXPECT_EQ(10968, offsetof(struct cras_server_state, last_active_stream_time));
  EXPECT_EQ(10984, offsetof(struct cras_server_state, audio_debug_info));
  EXPECT_EQ(135488,
            offsetof(struct cras_server_state, default_output_buffer_size));
  EXPECT_EQ(135492, offsetof(struct cras_server_state, non_empty_status));
  EXPECT_EQ(135496, offsetof(struct cras_server_state, aec_supported));
  EXPECT_EQ(135500, offsetof(struct cras_server_state, aec_group_id));
  EXPECT_EQ(135504, offsetof(struct cras_server_state, snapshot_buffer));
  EXPECT_EQ(1380748, offsetof(struct cras_server_state, bt_debug_info));
  EXPECT_EQ(1397240, offsetof(struct cras_server_state, bt_wbs_enabled));
  EXPECT_EQ(1397248,
            offsetof(struct cras_server_state, deprioritize_bt_wbs_mic));
  EXPECT_EQ(1397252,
            offsetof(struct cras_server_state, main_thread_debug_info));
  EXPECT_EQ(1417740, offsetof(struct cras_server_state,
                              num_input_streams_with_permission));
}
