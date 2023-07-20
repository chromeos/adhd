// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cras_types.h"
#include "gtest/gtest.h"

TEST(ABI, CrasServerState) {
#ifdef __arm__
  // TODO(b/291875693): Undo skip.
  GTEST_SKIP() << "Broken on ARM32";
#endif
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
  EXPECT_EQ(1656, offsetof(struct cras_server_state, input_devs));
  EXPECT_EQ(3256, offsetof(struct cras_server_state, num_output_nodes));
  EXPECT_EQ(3260, offsetof(struct cras_server_state, num_input_nodes));
  EXPECT_EQ(3264, offsetof(struct cras_server_state, output_nodes));
  EXPECT_EQ(6864, offsetof(struct cras_server_state, input_nodes));
  EXPECT_EQ(10464, offsetof(struct cras_server_state, num_attached_clients));
  EXPECT_EQ(10468, offsetof(struct cras_server_state, client_info));
  EXPECT_EQ(10788, offsetof(struct cras_server_state, update_count));
  EXPECT_EQ(10792, offsetof(struct cras_server_state, num_active_streams));
  EXPECT_EQ(10808, offsetof(struct cras_server_state, last_active_stream_time));
  EXPECT_EQ(10824, offsetof(struct cras_server_state, audio_debug_info));
  EXPECT_EQ(135328,
            offsetof(struct cras_server_state, default_output_buffer_size));
  EXPECT_EQ(135332, offsetof(struct cras_server_state, non_empty_status));
  EXPECT_EQ(135336, offsetof(struct cras_server_state, aec_supported));
  EXPECT_EQ(135340, offsetof(struct cras_server_state, aec_group_id));
  EXPECT_EQ(135344, offsetof(struct cras_server_state, snapshot_buffer));
  EXPECT_EQ(1380588, offsetof(struct cras_server_state, bt_debug_info));
  EXPECT_EQ(1397080, offsetof(struct cras_server_state, bt_wbs_enabled));
  EXPECT_EQ(1397088,
            offsetof(struct cras_server_state, deprioritize_bt_wbs_mic));
  EXPECT_EQ(1397092,
            offsetof(struct cras_server_state, main_thread_debug_info));
  EXPECT_EQ(1417580, offsetof(struct cras_server_state,
                              num_input_streams_with_permission));
}
