/* Copyright 2017 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef CRAS_SRC_SERVER_CONFIG_CRAS_BOARD_CONFIG_H_
#define CRAS_SRC_SERVER_CONFIG_CRAS_BOARD_CONFIG_H_

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

struct cras_board_config {
  int32_t default_output_buffer_size;
  int32_t aec_supported;
  int32_t aec_group_id;
  int32_t ns_supported;
  int32_t agc_supported;
  int32_t nc_supported;
  int32_t nc_standalone_mode;
  int32_t bt_wbs_enabled;
  int32_t bt_hfp_offload_finch_applied;
  int32_t deprioritize_bt_wbs_mic;
  char* ucm_ignore_suffix;
  int32_t hotword_pause_at_suspend;
  int32_t hw_echo_ref_disabled;
  int32_t aec_on_dsp_supported;
  int32_t ns_on_dsp_supported;
  int32_t agc_on_dsp_supported;
  int32_t max_internal_mic_gain;
  int32_t max_internal_speaker_channels;
  int32_t max_headphone_channels;
  int32_t speaker_output_latency_offset_ms;
  char* dsp_offload_map;
};

/* Creates a configuration based on the config file specified.
 * Args:
 *    config_path - Path containing the config files.
 * Returns:
 *    The created board config or NULL if memory allocation failed.
 */
struct cras_board_config* cras_board_config_create(const char* config_path);

/* Frees the board config and its internal members. */
void cras_board_config_destroy(struct cras_board_config* board_config);

#ifdef __cplusplus
}
#endif

#endif  // CRAS_SRC_SERVER_CONFIG_CRAS_BOARD_CONFIG_H_
