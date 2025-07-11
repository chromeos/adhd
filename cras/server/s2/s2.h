// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Generated from files in cras/src/server/rust in adhd.
// clang-format off

#ifdef __cplusplus
extern "C" {
#endif

#ifndef CRAS_SERVER_S2_S2_H_
#define CRAS_SERVER_S2_S2_H_

#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include "cras/common/rust_common.h"

typedef void (*NotifyAudioEffectUIAppearanceChanged)(struct CrasEffectUIAppearance);

typedef void (*ResetIodevListForVoiceIsolation)(void);

typedef void (*ReloadOutputPluginProcessor)(void);

struct CrasBoardName128 {
  char value[128];
};

bool cras_s2_is_locked_for_test(void);

void cras_s2_set_ap_nc_featured_allowed(bool allowed);

void cras_s2_set_ap_nc_segmentation_allowed(bool allowed);

bool cras_s2_are_audio_effects_ready(void);

void cras_s2_set_notify_audio_effect_ui_appearance_changed(NotifyAudioEffectUIAppearanceChanged notify_audio_effect_ui_appearance_changed);

void cras_s2_set_reset_iodev_list_for_voice_isolation(ResetIodevListForVoiceIsolation reset_iodev_list_for_voice_isolation);

void cras_s2_set_reload_output_plugin_processor(ReloadOutputPluginProcessor reload_output_plugin_processor);

char *cras_s2_get_audio_effect_dlcs(void);

/**
 * # Safety
 *
 * dlc must be a NULL terminated string.
 */
void cras_s2_set_dlc_installed_for_test(const char *dlc, bool installed);

bool cras_s2_get_ap_nc_allowed(void);

void cras_s2_set_style_transfer_featured_allowed(bool allowed);

bool cras_s2_get_style_transfer_allowed(void);

bool cras_s2_get_style_transfer_supported(void);

void cras_s2_set_voice_isolation_ui_preferred_effect(EFFECT_TYPE preferred_effect);

void cras_s2_set_voice_isolation_ui_enabled(bool enabled);

bool cras_s2_get_voice_isolation_ui_enabled(void);

void cras_s2_set_output_plugin_processor_enabled(bool enabled);

bool cras_s2_get_output_plugin_processor_enabled(void);

void cras_s2_init(void);

/**
 * # Safety
 *
 * board_name and cpu_name must be NULL-terminated strings or NULLs.
 */
void cras_s2_init_with_board_and_cpu_for_test(const char *board_name, const char *cpu_name);

bool cras_s2_get_beamforming_supported(void);

bool cras_s2_get_beamforming_allowed(void);

char *cras_s2_dump_json(void);

void cras_s2_reset_for_testing(void);

void cras_s2_set_nc_standalone_mode(bool nc_standalone_mode);

bool cras_s2_get_nc_standalone_mode(void);

void cras_s2_set_dsp_nc_supported(bool dsp_nc_supported);

bool cras_s2_get_dsp_nc_supported(void);

void cras_s2_set_non_dsp_aec_echo_ref_dev_alive(bool non_dsp_aec_echo_ref_dev_alive);

bool cras_s2_get_non_dsp_aec_echo_ref_dev_alive(void);

void cras_s2_set_aec_on_dsp_is_disallowed(bool aec_on_dsp_is_disallowed);

bool cras_s2_get_aec_on_dsp_is_disallowed(void);

bool cras_s2_get_dsp_input_effects_blocked(void);

void cras_s2_set_bypass_block_dsp_nc(bool bypass_block_dsp_nc);

bool cras_s2_get_bypass_block_dsp_nc(void);

void cras_s2_set_active_input_node_compatible_nc_providers(CRAS_NC_PROVIDER compatible_nc_providers);

enum CrasProcessorEffect cras_s2_get_cras_processor_effect(CRAS_NC_PROVIDER compatible_nc_providers,
                                                           bool client_controlled,
                                                           bool client_enabled);

CRAS_NC_PROVIDER cras_s2_get_iodev_restart_tag_for_nc_providers(CRAS_NC_PROVIDER compatible_nc_providers);

struct CrasEffectUIAppearance cras_s2_get_audio_effect_ui_appearance(void);

void cras_s2_set_spatial_audio_enabled(bool enabled);

bool cras_s2_get_spatial_audio_enabled(void);

void cras_s2_set_spatial_audio_supported(bool supported);

bool cras_s2_get_spatial_audio_supported(void);

bool cras_s2_get_sr_bt_supported(void);

struct CrasBoardName128 cras_s2_get_board_name(void);

#endif  /* CRAS_SERVER_S2_S2_H_ */

#ifdef __cplusplus
}
#endif
