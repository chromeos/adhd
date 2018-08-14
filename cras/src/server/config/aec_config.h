/* Copyright 2018 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef AEC_CONFIG_H_
#define AEC_CONFIG_H_

#include <webrtc-apm/webrtc_apm.h>

#define AEC_DELAY_DEFAULT_DELAY "delay:default_delay"
#define AEC_DELAY_DEFAULT_DELAY_VALUE 5
#define AEC_DELAY_DOWN_SAMPLING_FACTOR "delay:down_sampling_factor"
#define AEC_DELAY_DOWN_SAMPLING_FACTOR_VALUE 4
#define AEC_DELAY_NUM_FILTERS "delay:num_filters"
#define AEC_DELAY_NUM_FILTERS_VALUE 6
#define AEC_DELAY_API_CALL_JITTER_BLOCKS "delay:api_call_jitter_blocks"
#define AEC_DELAY_API_CALL_JITTER_BLOCKS_VALUE 26
#define AEC_DELAY_MIN_ECHO_PATH_DELAY_BLOCKS "delay:min_echo_path_delay_blocks"
#define AEC_DELAY_MIN_ECHO_PATH_DELAY_BLOCKS_VALUE 0
#define AEC_DELAY_DELAY_HEADROOM_BLOCKS "delay:delay_headroom_blocks"
#define AEC_DELAY_DELAY_HEADROOM_BLOCKS_VALUE 2
#define AEC_DELAY_HYSTERESIS_LIMIT_1_BLOCKS "delay:hysteresis_limit_1_blocks"
#define AEC_DELAY_HYSTERESIS_LIMIT_1_BLOCKS_VALUE 1
#define AEC_DELAY_HYSTERESIS_LIMIT_2_BLOCKS "delay:hysteresis_limit_2_blocks"
#define AEC_DELAY_HYSTERESIS_LIMIT_2_BLOCKS_VALUE 1
#define AEC_DELAY_SKEW_HYSTERESIS_BLOCKS "delay:skew_hysteresis_blocks"
#define AEC_DELAY_SKEW_HYSTERESIS_BLOCKS_VALUE 3

// Filter Main configuration
#define AEC_FILTER_MAIN_LENGTH_BLOCKS "filter.main:length_blocks"
#define AEC_FILTER_MAIN_LENGTH_BLOCKS_VALUE 13
#define AEC_FILTER_MAIN_LEAKAGE_CONVERGED "filter.main:leakage_converged"
#define AEC_FILTER_MAIN_LEAKAGE_CONVERGED_VALUE 0.00005f
#define AEC_FILTER_MAIN_LEAKAGE_DIVERGED "filter.main:leakage_diverged"
#define AEC_FILTER_MAIN_LEAKAGE_DIVERGED_VALUE 0.01f
#define AEC_FILTER_MAIN_ERROR_FLOOR "filter.main:error_floor"
#define AEC_FILTER_MAIN_ERROR_FLOOR_VALUE 0.1f
#define AEC_FILTER_MAIN_NOISE_GATE "filter.main:noise_gate"
#define AEC_FILTER_MAIN_NOISE_GATE_VALUE 20075344.f

// Filter Shadow configuration
#define AEC_FILTER_SHADOW_LENGTH_BLOCKS "filter.shadow:length_blocks"
#define AEC_FILTER_SHADOW_LENGTH_BLOCKS_VALUE 13
#define AEC_FILTER_SHADOW_RATE "filter.shadow:rate"
#define AEC_FILTER_SHADOW_RATE_VALUE 0.7f
#define AEC_FILTER_SHADOW_NOISE_GATE "filter.shadow:noise_gate"
#define AEC_FILTER_SHADOW_NOISE_GATE_VALUE 20075344.f

// Filter Main initial configuration
#define AEC_FILTER_MAIN_INIT_LENGTH_BLOCKS "filter.main_initial:length_blocks"
#define AEC_FILTER_MAIN_INIT_LENGTH_BLOCKS_VALUE 12
#define AEC_FILTER_MAIN_INIT_LEAKAGE_CONVERGED "filter.main_initial:leakage_converged"
#define AEC_FILTER_MAIN_INIT_LEAKAGE_CONVERGED_VALUE 0.05f
#define AEC_FILTER_MAIN_INIT_LEAKAGE_DIVERGED "filter.main_initial:leakage_diverged"
#define AEC_FILTER_MAIN_INIT_LEAKAGE_DIVERGED_VALUE 5.f
#define AEC_FILTER_MAIN_INIT_ERROR_FLOOR "filter.main_initial:error_floor"
#define AEC_FILTER_MAIN_INIT_ERROR_FLOOR_VALUE 0.001f
#define AEC_FILTER_MAIN_INIT_NOISE_GATE "filter.main_initial:noise_gate"
#define AEC_FILTER_MAIN_INIT_NOISE_GATE_VALUE 20075344.f

// Filter Shadow initial configuration
#define AEC_FILTER_SHADOW_INIT_LENGTH_BLOCKS "filter.shadow_initial:length_blocks"
#define AEC_FILTER_SHADOW_INIT_LENGTH_BLOCKS_VALUE 12
#define AEC_FILTER_SHADOW_INIT_RATE "filter.shadow_initial:rate"
#define AEC_FILTER_SHADOW_INIT_RATE_VALUE 0.9f
#define AEC_FILTER_SHADOW_INIT_NOISE_GATE "filter.shadow_initial:noise_gate"
#define AEC_FILTER_SHADOW_INIT_NOISE_GATE_VALUE 20075344.f
#define AEC_FILTER_CONFIG_CHANGE_DURATION_BLOCKS "filter:config_change_duration_blocks"
#define AEC_FILTER_CONFIG_CHANGE_DURATION_BLOCKS_VALUE 250

// Erle
#define AEC_ERLE_MIN "erle:min"
#define AEC_ERLE_MIN_VALUE 1.f
#define AEC_ERLE_MAX_L "erle:max_l"
#define AEC_ERLE_MAX_L_VALUE 4.f
#define AEC_ERLE_MAX_H "erle:max_h"
#define AEC_ERLE_MAX_H_VALUE 1.5f

// EpStrength
#define AEC_EP_STRENGTH_LF "ep_strength:lf"
#define AEC_EP_STRENGTH_LF_VALUE 1.f
#define AEC_EP_STRENGTH_MF "ep_strength:mf"
#define AEC_EP_STRENGTH_MF_VALUE 1.f
#define AEC_EP_STRENGTH_HF "ep_strength:hf"
#define AEC_EP_STRENGTH_HF_VALUE 1.f
#define AEC_EP_STRENGTH_DEFAULT_LEN "ep_strength:default_len"
#define AEC_EP_STRENGTH_DEFAULT_LEN_VALUE 0.88f
#define AEC_EP_STRENGTH_REVERB_BASED_ON_RENDER "ep_strength:reverb_based_on_render"
#define AEC_EP_STRENGTH_REVERB_BASED_ON_RENDER_VALUE 1
#define AEC_EP_STRENGTH_BOUNDED_ERL "ep_strength:bounded_erl"
#define AEC_EP_STRENGTH_BOUNDED_ERL_VALUE 0
#define AEC_EP_STRENGTH_ECHO_CAN_SATURATE "ep_strength:echo_can_saturate"
#define AEC_EP_STRENGTH_ECHO_CAN_SATURATE_VALUE 1

// Gain mask
#define AEC_GAIN_MASK_M0 "gain_mask:m0"
#define AEC_GAIN_MASK_M0_VALUE 0.1f
#define AEC_GAIN_MASK_M1 "gain_mask:m1"
#define AEC_GAIN_MASK_M1_VALUE 0.01f
#define AEC_GAIN_MASK_M2 "gain_mask:m2"
#define AEC_GAIN_MASK_M2_VALUE 0.0001f
#define AEC_GAIN_MASK_M3 "gain_mask:m3"
#define AEC_GAIN_MASK_M3_VALUE 0.01f
// m4 was removed intentionally.
// https://webrtc-review.googlesource.com/c/src/+/70421
#define AEC_GAIN_MASK_M5 "gain_mask:m5"
#define AEC_GAIN_MASK_M5_VALUE 0.01f
#define AEC_GAIN_MASK_M6 "gain_mask:m6"
#define AEC_GAIN_MASK_M6_VALUE 0.0001f
#define AEC_GAIN_MASK_M7 "gain_mask:m7"
#define AEC_GAIN_MASK_M7_VALUE 0.01f
#define AEC_GAIN_MASK_M8 "gain_mask:m8"
#define AEC_GAIN_MASK_M8_VALUE 0.0001f
#define AEC_GAIN_MASK_M9 "gain_mask:m9"
#define AEC_GAIN_MASK_M9_VALUE 0.1f
#define AEC_GAIN_MASK_GAIN_CURVE_OFFSET "gain_mask:gain_curve_offset"
#define AEC_GAIN_MASK_GAIN_CURVE_OFFSET_VALUE 1.45f
#define AEC_GAIN_MASK_GAIN_CURVE_SLOPE "gain_mask:gain_curve_slope"
#define AEC_GAIN_MASK_GAIN_CURVE_SLOPE_VALUE 5.f
#define AEC_GAIN_MASK_TEMPORAL_MASKING_LF "gain_mask:temporal_masking_lf"
#define AEC_GAIN_MASK_TEMPORAL_MASKING_LF_VALUE 0.9f
#define AEC_GAIN_MASK_TEMPORAL_MASKING_HF "gain_mask:temporal_masking_hf"
#define AEC_GAIN_MASK_TEMPORAL_MASKING_HF_VALUE 0.6f
#define AEC_GAIN_MASK_TEMPORAL_MASKING_LF_BANDS "gain_mask:temporal_masking_lf_bands"
#define AEC_GAIN_MASK_TEMPORAL_MASKING_LF_BANDS_VALUE 3

#define AEC_ECHO_AUDIBILITY_LOW_RENDER_LIMIT "echo_audibility:low_render_limit"
#define AEC_ECHO_AUDIBILITY_LOW_RENDER_LIMIT_VALUE 4 * 64.f
#define AEC_ECHO_AUDIBILITY_NORMAL_RENDER_LIMIT "echo_audibility:normal_render_limit"
#define AEC_ECHO_AUDIBILITY_NORMAL_RENDER_LIMIT_VALUE 64.f
#define AEC_ECHO_AUDIBILITY_FLOOR_POWER "echo_audibility:floor_power"
#define AEC_ECHO_AUDIBILITY_FLOOR_POWER_VALUE 2 * 64.f
#define AEC_ECHO_AUDIBILITY_AUDIBILITY_THRESHOLD_LF "echo_audibility:audibility_threshold_lf"
#define AEC_ECHO_AUDIBILITY_AUDIBILITY_THRESHOLD_LF_VALUE 10
#define AEC_ECHO_AUDIBILITY_AUDIBILITY_THRESHOLD_MF "echo_audibility:audibility_threshold_mf"
#define AEC_ECHO_AUDIBILITY_AUDIBILITY_THRESHOLD_MF_VALUE 10
#define AEC_ECHO_AUDIBILITY_AUDIBILITY_THRESHOLD_HF "echo_audibility:audibility_threshold_hf"
#define AEC_ECHO_AUDIBILITY_AUDIBILITY_THRESHOLD_HF_VALUE 10
#define AEC_ECHO_AUDIBILITY_USE_STATIONARY_PROPERTIES "echo_audibility:use_stationary_properties"
#define AEC_ECHO_AUDIBILITY_USE_STATIONARY_PROPERTIES_VALUE 1

// Rendering levels
#define AEC_RENDER_LEVELS_ACTIVE_RENDER_LIMIT "render_levels:active_render_limit"
#define AEC_RENDER_LEVELS_ACTIVE_RENDER_LIMIT_VALUE 100.f
#define AEC_RENDER_LEVELS_POOR_EXCITATION_RENDER_LIMIT \
	"render_levels:poor_excitation_render_limit"
#define AEC_RENDER_LEVELS_POOR_EXCITATION_RENDER_LIMIT_VALUE 150.f
#define AEC_RENDER_LEVELS_POOR_EXCITATION_RENDER_LIMIT_DS8 \
	"render_levels:poor_excitation_render_limit_ds8"
#define AEC_RENDER_LEVELS_POOR_EXCITATION_RENDER_LIMIT_DS8_VALUE 20.f

// GainUpdates
#define AEC_GAIN_UPDATES_LOW_NOISE_MAX_INC "gain_updates.low_noise:max_inc"
#define AEC_GAIN_UPDATES_LOW_NOISE_MAX_INC_VALUE 2.f
#define AEC_GAIN_UPDATES_LOW_NOISE_MAX_DEC "gain_updates.low_noise:max_dec"
#define AEC_GAIN_UPDATES_LOW_NOISE_MAX_DEC_VALUE 2.f
#define AEC_GAIN_UPDATES_LOW_NOISE_RATE_INC "gain_updates.low_noise:rate_inc"
#define AEC_GAIN_UPDATES_LOW_NOISE_RATE_INC_VALUE 1.4f
#define AEC_GAIN_UPDATES_LOW_NOISE_RATE_DEC "gain_updates.low_noise:rate_dec"
#define AEC_GAIN_UPDATES_LOW_NOISE_RATE_DEC_VALUE 1.4f
#define AEC_GAIN_UPDATES_LOW_NOISE_MIN_INC "gain_updates.low_noise:min_inc"
#define AEC_GAIN_UPDATES_LOW_NOISE_MIN_INC_VALUE 1.1f
#define AEC_GAIN_UPDATES_LOW_NOISE_MIN_DEC "gain_updates.low_noise:min_dec"
#define AEC_GAIN_UPDATES_LOW_NOISE_MIN_DEC_VALUE 1.1f

#define AEC_GAIN_UPDATES_INITIAL_MAX_INC "gain_updates.initial:max_inc"
#define AEC_GAIN_UPDATES_INITIAL_MAX_INC_VALUE 2.f
#define AEC_GAIN_UPDATES_INITIAL_MAX_DEC "gain_updates.initial:max_dec"
#define AEC_GAIN_UPDATES_INITIAL_MAX_DEC_VALUE 2.f
#define AEC_GAIN_UPDATES_INITIAL_RATE_INC "gain_updates.initial:rate_inc"
#define AEC_GAIN_UPDATES_INITIAL_RATE_INC_VALUE 1.5f
#define AEC_GAIN_UPDATES_INITIAL_RATE_DEC "gain_updates.initial:rate_dec"
#define AEC_GAIN_UPDATES_INITIAL_RATE_DEC_VALUE 1.5f
#define AEC_GAIN_UPDATES_INITIAL_MIN_INC "gain_updates.initial:min_inc"
#define AEC_GAIN_UPDATES_INITIAL_MIN_INC_VALUE 1.2f
#define AEC_GAIN_UPDATES_INITIAL_MIN_DEC "gain_updates.initial:min_dec"
#define AEC_GAIN_UPDATES_INITIAL_MIN_DEC_VALUE 1.2f

#define AEC_GAIN_UPDATES_NORMAL_MAX_INC "gain_updates.normal:max_inc"
#define AEC_GAIN_UPDATES_NORMAL_MAX_INC_VALUE 2.f
#define AEC_GAIN_UPDATES_NORMAL_MAX_DEC "gain_updates.normal:max_dec"
#define AEC_GAIN_UPDATES_NORMAL_MAX_DEC_VALUE 2.f
#define AEC_GAIN_UPDATES_NORMAL_RATE_INC "gain_updates.normal:rate_inc"
#define AEC_GAIN_UPDATES_NORMAL_RATE_INC_VALUE 1.5f
#define AEC_GAIN_UPDATES_NORMAL_RATE_DEC "gain_updates.normal:rate_dec"
#define AEC_GAIN_UPDATES_NORMAL_RATE_DEC_VALUE 1.5f
#define AEC_GAIN_UPDATES_NORMAL_MIN_INC "gain_updates.normal:min_inc"
#define AEC_GAIN_UPDATES_NORMAL_MIN_INC_VALUE 1.2f
#define AEC_GAIN_UPDATES_NORMAL_MIN_DEC "gain_updates.normal:min_dec"
#define AEC_GAIN_UPDATES_NORMAL_MIN_DEC_VALUE 1.2f

#define AEC_GAIN_UPDATES_SATURATION_MAX_INC "gain_updates.saturation:max_inc"
#define AEC_GAIN_UPDATES_SATURATION_MAX_INC_VALUE 1.2f
#define AEC_GAIN_UPDATES_SATURATION_MAX_DEC "gain_updates.saturation:max_dec"
#define AEC_GAIN_UPDATES_SATURATION_MAX_DEC_VALUE 1.2f
#define AEC_GAIN_UPDATES_SATURATION_RATE_INC "gain_updates.saturation:rate_inc"
#define AEC_GAIN_UPDATES_SATURATION_RATE_INC_VALUE 1.5f
#define AEC_GAIN_UPDATES_SATURATION_RATE_DEC "gain_updates.saturation:rate_dec"
#define AEC_GAIN_UPDATES_SATURATION_RATE_DEC_VALUE 1.5f
#define AEC_GAIN_UPDATES_SATURATION_MIN_INC "gain_updates.saturation:min_inc"
#define AEC_GAIN_UPDATES_SATURATION_MIN_INC_VALUE 1.f
#define AEC_GAIN_UPDATES_SATURATION_MIN_DEC "gain_updates.saturation:min_dec"
#define AEC_GAIN_UPDATES_SATURATION_MIN_DEC_VALUE 1.f

#define AEC_GAIN_UPDATES_NONLINEAR_MAX_INC "gain_updates.nonlinear:max_inc"
#define AEC_GAIN_UPDATES_NONLINEAR_MAX_INC_VALUE 1.5f
#define AEC_GAIN_UPDATES_NONLINEAR_MAX_DEC "gain_updates.nonlinear:max_dec"
#define AEC_GAIN_UPDATES_NONLINEAR_MAX_DEC_VALUE 1.5f
#define AEC_GAIN_UPDATES_NONLINEAR_RATE_INC "gain_updates.nonlinear:rate_inc"
#define AEC_GAIN_UPDATES_NONLINEAR_RATE_INC_VALUE 1.2f
#define AEC_GAIN_UPDATES_NONLINEAR_RATE_DEC "gain_updates.nonlinear:rate_dec"
#define AEC_GAIN_UPDATES_NONLINEAR_RATE_DEC_VALUE 1.2f
#define AEC_GAIN_UPDATES_NONLINEAR_MIN_INC "gain_updates.nonlinear:min_inc"
#define AEC_GAIN_UPDATES_NONLINEAR_MIN_INC_VALUE 1.1f
#define AEC_GAIN_UPDATES_NONLINEAR_MIN_DEC "gain_updates.nonlinear:min_dec"
#define AEC_GAIN_UPDATES_NONLINEAR_MIN_DEC_VALUE 1.1f

#define AEC_GAIN_UPDATES_MAX_INC_FACTOR "gain_updates:max_inc_factor"
#define AEC_GAIN_UPDATES_MAX_INC_FACTOR_VALUE 2.0f
#define AEC_GAIN_UPDATES_MAX_DEC_FACTOR_LF "gain_updates:max_dec_factor_lf"
#define AEC_GAIN_UPDATES_MAX_DEC_FACTOR_LF_VALUE 0.25f
#define AEC_GAIN_UPDATES_FLOOR_FIRST_INCREASE "gain_updates:floor_first_increase"
#define AEC_GAIN_UPDATES_FLOOR_FIRST_INCREASE_VALUE 0.00001f

// Echo removal controls
#define AEC_ECHO_REMOVAL_CTL_INITIAL_GAIN "echo_removal_control:initial_gain"
#define AEC_ECHO_REMOVAL_CTL_INITIAL_GAIN_VALUE 0.0f
#define AEC_ECHO_REMOVAL_CTL_FIRST_NON_ZERO_GAIN "echo_removal_control:first_non_zero_gain"
#define AEC_ECHO_REMOVAL_CTL_FIRST_NON_ZERO_GAIN_VALUE 0.001f
#define AEC_ECHO_REMOVAL_CTL_NON_ZERO_GAIN_BLOCKS "echo_removal_control:non_zero_gain_blocks"
#define AEC_ECHO_REMOVAL_CTL_NON_ZERO_GAIN_BLOCKS_VALUE 187
#define AEC_ECHO_REMOVAL_CTL_FULL_GAIN_BLOCKS "echo_removal_control:full_gain_blocks"
#define AEC_ECHO_REMOVAL_CTL_FULL_GAIN_BLOCKS_VALUE 312
#define AEC_ECHO_REMOVAL_CTL_HAS_CLOCK_DRIFT "echo_removal_control:has_clock_drift"
#define AEC_ECHO_REMOVAL_CTL_HAS_CLOCK_DRIFT_VALUE 0
#define AEC_ECHO_REMOVAL_CTL_LINEAR_AND_STABLE_ECHO_PATH \
	"echo_removal_control:linear_and_stable_echo_path"
#define AEC_ECHO_REMOVAL_CTL_LINEAR_AND_STABLE_ECHO_PATH_VALUE 0

// EchoModel
#define AEC_ECHO_MODEL_NOISE_FLOOR_HOLD "echo_model:noise_floor_hold"
#define AEC_ECHO_MODEL_NOISE_FLOOR_HOLD_VALUE 50
#define AEC_ECHO_MODEL_MIN_NOISE_FLOOR_POWER "echo_model:min_noise_floor_power"
#define AEC_ECHO_MODEL_MIN_NOISE_FLOOR_POWER_VALUE 1638400.f
#define AEC_ECHO_MODEL_STATIONARY_GATE_SLOPE "echo_model:stationary_gate_slope"
#define AEC_ECHO_MODEL_STATIONARY_GATE_SLOPE_VALUE 10.f
#define AEC_ECHO_MODEL_NOISE_GATE_POWER "echo_model:noise_gate_power"
#define AEC_ECHO_MODEL_NOISE_GATE_POWER_VALUE 27509.42f
#define AEC_ECHO_MODEL_NOISE_GATE_SLOPE "echo_model:noise_gate_slope"
#define AEC_ECHO_MODEL_NOISE_GATE_SLOPE_VALUE 0.3f
#define AEC_ECHO_MODEL_RENDER_PRE_WINDOW_SIZE "echo_model:render_pre_window_size"
#define AEC_ECHO_MODEL_RENDER_PRE_WINDOW_SIZE_VALUE 1
#define AEC_ECHO_MODEL_RENDER_POST_WINDOW_SIZE "echo_model:render_post_window_size"
#define AEC_ECHO_MODEL_RENDER_POST_WINDOW_SIZE_VALUE 1
#define AEC_ECHO_MODEL_RENDER_PRE_WINDOW_SIZE_INIT "echo_model:render_pre_window_size_init"
#define AEC_ECHO_MODEL_RENDER_PRE_WINDOW_SIZE_INIT_VALUE 10
#define AEC_ECHO_MODEL_RENDER_POST_WINDOW_SIZE_INIT "echo_model:render_post_window_size_init"
#define AEC_ECHO_MODEL_RENDER_POST_WINDOW_SIZE_INIT_VALUE 10
#define AEC_ECHO_MODEL_NONLINEAR_HOLD "echo_model:nonlinear_hold"
#define AEC_ECHO_MODEL_NONLINEAR_HOLD_VALUE 1
#define AEC_ECHO_MODEL_NONLINEAR_RELEASE "echo_model:nonlinear_release"
#define AEC_ECHO_MODEL_NONLINEAR_RELEASE_VALUE 0.001f

#define AEC_SUPPRESSOR_BANDS_WITH_RELIABLE_COHERENCE "suppressor:bands_with_reliable_coherence"
#define AEC_SUPPRESSOR_BANDS_WITH_RELIABLE_COHERENCE_VALUE 5
#define AEC_SUPPRESSOR_NEAREND_AVERAGE_BLOCKS "suppressor:nearend_average_blocks"
#define AEC_SUPPRESSOR_NEAREND_AVERAGE_BLOCKS_VALUE 4

#define AEC_SUPPRESSOR_MASK_LF_ENR_TRANSPARENT "suppressor:mask_lf_enr_transparent"
#define AEC_SUPPRESSOR_MASK_LF_ENR_TRANSPARENT_VALUE .2f
#define AEC_SUPPRESSOR_MASK_LF_ENR_SUPPRESS "suppressor:mask_lf_enr_suppress"
#define AEC_SUPPRESSOR_MASK_LF_ENR_SUPPRESS_VALUE .3f
#define AEC_SUPPRESSOR_MASK_LF_EMR_TRANSPARENT "suppressor:mask_lf_emr_transparent"
#define AEC_SUPPRESSOR_MASK_LF_EMR_TRANSPARENT_VALUE .3f

#define AEC_SUPPRESSOR_MASK_HF_ENR_TRANSPARENT "suppressor:mask_hf_enr_transparent"
#define AEC_SUPPRESSOR_MASK_HF_ENR_TRANSPARENT_VALUE .07f
#define AEC_SUPPRESSOR_MASK_HF_ENR_SUPPRESS "suppressor:mask_hf_enr_suppress"
#define AEC_SUPPRESSOR_MASK_HF_ENR_SUPPRESS_VALUE .1f
#define AEC_SUPPRESSOR_MASK_HF_EMR_TRANSPARENT "suppressor:mask_hf_emr_transparent"
#define AEC_SUPPRESSOR_MASK_HF_EMR_TRANSPARENT_VALUE .3f

/* */
struct aec_config *aec_config_get(const char *device_config_dir);

/* */
void aec_config_dump(struct aec_config *config);

#endif /* AEC_CONFIG_H_ */
