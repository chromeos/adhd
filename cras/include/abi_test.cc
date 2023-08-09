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
  EXPECT_EQ(44, offsetof(struct cras_server_state, aec_supported));
  EXPECT_EQ(48, offsetof(struct cras_server_state, aec_group_id));
  EXPECT_EQ(52, offsetof(struct cras_server_state, num_streams_attached));
  EXPECT_EQ(56, offsetof(struct cras_server_state, num_output_devs));
  EXPECT_EQ(60, offsetof(struct cras_server_state, num_input_devs));
  EXPECT_EQ(64, offsetof(struct cras_server_state, output_devs));
  EXPECT_EQ(1744, offsetof(struct cras_server_state, input_devs));
  EXPECT_EQ(3424, offsetof(struct cras_server_state, num_output_nodes));
  EXPECT_EQ(3428, offsetof(struct cras_server_state, num_input_nodes));
  EXPECT_EQ(3432, offsetof(struct cras_server_state, output_nodes));
  EXPECT_EQ(7032, offsetof(struct cras_server_state, input_nodes));
  EXPECT_EQ(10632, offsetof(struct cras_server_state, num_attached_clients));
  EXPECT_EQ(10636, offsetof(struct cras_server_state, client_info));
  EXPECT_EQ(10956, offsetof(struct cras_server_state, update_count));
  EXPECT_EQ(10960, offsetof(struct cras_server_state, num_active_streams));
  EXPECT_EQ(10976, offsetof(struct cras_server_state, last_active_stream_time));
  EXPECT_EQ(10992,
            offsetof(struct cras_server_state, default_output_buffer_size));
  EXPECT_EQ(10996, offsetof(struct cras_server_state, non_empty_status));
  EXPECT_EQ(11000, offsetof(struct cras_server_state, snapshot_buffer));
  EXPECT_EQ(1256244, offsetof(struct cras_server_state, bt_wbs_enabled));
  EXPECT_EQ(1256248,
            offsetof(struct cras_server_state, bt_hfp_offload_finch_applied));
  EXPECT_EQ(1256252,
            offsetof(struct cras_server_state, deprioritize_bt_wbs_mic));
  EXPECT_EQ(1256256,
            offsetof(struct cras_server_state, noise_cancellation_enabled));
  EXPECT_EQ(1256260, offsetof(struct cras_server_state,
                              dsp_noise_cancellation_supported));
  EXPECT_EQ(1256264, offsetof(struct cras_server_state,
                              bypass_block_noise_cancellation));
  EXPECT_EQ(1256268,
            offsetof(struct cras_server_state, hotword_pause_at_suspend));
  EXPECT_EQ(1256272, offsetof(struct cras_server_state, ns_supported));
  EXPECT_EQ(1256276, offsetof(struct cras_server_state, agc_supported));
  EXPECT_EQ(1256280, offsetof(struct cras_server_state, hw_echo_ref_disabled));
  EXPECT_EQ(1256284, offsetof(struct cras_server_state, max_internal_mic_gain));
  EXPECT_EQ(1256288, offsetof(struct cras_server_state, aec_on_dsp_supported));
  EXPECT_EQ(1256292, offsetof(struct cras_server_state, ns_on_dsp_supported));
  EXPECT_EQ(1256296, offsetof(struct cras_server_state, agc_on_dsp_supported));
  EXPECT_EQ(1256300,
            offsetof(struct cras_server_state, force_respect_ui_gains));
  EXPECT_EQ(1256304, offsetof(struct cras_server_state, active_node_type_pair));
  EXPECT_EQ(1256372,
            offsetof(struct cras_server_state, max_internal_speaker_channels));
  EXPECT_EQ(1256376,
            offsetof(struct cras_server_state, max_headphone_channels));
  EXPECT_EQ(1256380,
            offsetof(struct cras_server_state, num_non_chrome_output_streams));
  EXPECT_EQ(1256384, offsetof(struct cras_server_state, nc_standalone_mode));
  EXPECT_EQ(1256388,
            offsetof(struct cras_server_state, voice_isolation_supported));
  EXPECT_EQ(1256392, offsetof(struct cras_server_state,
                              num_input_streams_with_permission));
}
