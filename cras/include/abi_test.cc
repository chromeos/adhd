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
  EXPECT_EQ(11000, offsetof(struct cras_server_state, bt_wbs_enabled));
  EXPECT_EQ(11004,
            offsetof(struct cras_server_state, bt_hfp_offload_finch_applied));
  EXPECT_EQ(11008, offsetof(struct cras_server_state, deprioritize_bt_wbs_mic));
  EXPECT_EQ(11012,
            offsetof(struct cras_server_state, noise_cancellation_enabled));
  EXPECT_EQ(11016, offsetof(struct cras_server_state,
                            dsp_noise_cancellation_supported));
  EXPECT_EQ(11020, offsetof(struct cras_server_state,
                            bypass_block_noise_cancellation));
  EXPECT_EQ(11024,
            offsetof(struct cras_server_state, hotword_pause_at_suspend));
  EXPECT_EQ(11028, offsetof(struct cras_server_state, ns_supported));
  EXPECT_EQ(11032, offsetof(struct cras_server_state, agc_supported));
  EXPECT_EQ(11036, offsetof(struct cras_server_state, hw_echo_ref_disabled));
  EXPECT_EQ(11040, offsetof(struct cras_server_state, max_internal_mic_gain));
  EXPECT_EQ(11044, offsetof(struct cras_server_state, aec_on_dsp_supported));
  EXPECT_EQ(11048, offsetof(struct cras_server_state, ns_on_dsp_supported));
  EXPECT_EQ(11052, offsetof(struct cras_server_state, agc_on_dsp_supported));
  EXPECT_EQ(11056, offsetof(struct cras_server_state, force_respect_ui_gains));
  EXPECT_EQ(11060, offsetof(struct cras_server_state, active_node_type_pair));
  EXPECT_EQ(11128,
            offsetof(struct cras_server_state, max_internal_speaker_channels));
  EXPECT_EQ(11132, offsetof(struct cras_server_state, max_headphone_channels));
  EXPECT_EQ(11136,
            offsetof(struct cras_server_state, num_non_chrome_output_streams));
  EXPECT_EQ(11140, offsetof(struct cras_server_state, nc_standalone_mode));
  EXPECT_EQ(11144,
            offsetof(struct cras_server_state, voice_isolation_supported));
  EXPECT_EQ(11148, offsetof(struct cras_server_state,
                            num_input_streams_with_permission));
}
