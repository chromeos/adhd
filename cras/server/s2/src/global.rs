// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use std::collections::HashMap;
use std::ffi::c_char;
use std::ffi::CString;
use std::ops::Deref;
use std::sync::Mutex;
use std::sync::MutexGuard;
use std::sync::OnceLock;

use cras_common::types_internal::CrasDlcId;
use cras_common::types_internal::CrasEffectUIAppearance;
use cras_common::types_internal::CrasProcessorEffect;
use cras_common::types_internal::CRAS_NC_PROVIDER;

fn state() -> MutexGuard<'static, crate::S2> {
    static CELL: OnceLock<Mutex<crate::S2>> = OnceLock::new();
    CELL.get_or_init(|| Mutex::new(crate::S2::new()))
        .lock()
        .unwrap()
}

#[no_mangle]
pub extern "C" fn cras_s2_set_ap_nc_featured_allowed(allowed: bool) {
    state().set_ap_nc_featured_allowed(allowed);
}

#[no_mangle]
pub extern "C" fn cras_s2_set_ap_nc_segmentation_allowed(allowed: bool) {
    state().set_ap_nc_segmentation_allowed(allowed);
}

#[no_mangle]
pub extern "C" fn cras_s2_set_ap_nc_feature_tier_allowed(allowed: bool) {
    state().set_ap_nc_feature_tier_allowed(allowed);
}

pub fn set_dlc_manager_ready() {
    state().set_dlc_manager_ready();
}

#[no_mangle]
pub extern "C" fn cras_s2_are_audio_effects_ready() -> bool {
    state().output.audio_effects_ready
}

// Called when audio_effects_ready is changed, with the following arguments:
// - bool: indicates whether the audio effects is ready.
pub type NotifyAudioEffectUIAppearanceChanged = extern "C" fn(bool);

#[no_mangle]
pub extern "C" fn cras_s2_set_notify_audio_effect_ui_appearance_changed(
    notify_audio_effect_ui_appearance_changed: NotifyAudioEffectUIAppearanceChanged,
) {
    state()
        .set_notify_audio_effect_ui_appearance_changed(notify_audio_effect_ui_appearance_changed);
}

pub fn cras_s2_init_dlc_installed(dlc_installed: HashMap<CrasDlcId, bool>) {
    state().init_dlc_installed(dlc_installed);
}

#[no_mangle]
pub extern "C" fn cras_s2_get_audio_effect_dlcs() -> *mut c_char {
    // TODO(b/372393426): Consider returning protobuf instead of string.
    let s = state()
        .input
        .dlc_installed
        .as_ref()
        .unwrap_or(&HashMap::new())
        .keys()
        .cloned()
        .collect::<Vec<String>>()
        .join(",");
    CString::new(s).expect("CString::new").into_raw()
}

#[no_mangle]
pub extern "C" fn cras_s2_set_dlc_installed(dlc: CrasDlcId, installed: bool) {
    state().set_dlc_installed(dlc, installed);
}

#[no_mangle]
pub extern "C" fn cras_s2_get_ap_nc_allowed() -> bool {
    state().output.get_ap_nc_status().allowed
}

#[no_mangle]
pub extern "C" fn cras_s2_set_style_transfer_featured_allowed(allowed: bool) {
    state().set_style_transfer_featured_allowed(allowed);
}

#[no_mangle]
pub extern "C" fn cras_s2_get_style_transfer_allowed() -> bool {
    state().output.get_ast_status().allowed
}

#[no_mangle]
pub extern "C" fn cras_s2_get_style_transfer_supported() -> bool {
    state().output.get_ast_status().supported
}

#[no_mangle]
pub extern "C" fn cras_s2_set_voice_isolation_ui_enabled(enabled: bool) {
    state().set_voice_isolation_ui_enabled(enabled);
}

#[no_mangle]
pub extern "C" fn cras_s2_get_voice_isolation_ui_enabled() -> bool {
    state().input.voice_isolation_ui_enabled
}

#[no_mangle]
pub extern "C" fn cras_s2_init() {
    match std::fs::read_to_string("/run/chromeos-config/v1/audio/main/cras-config-dir") {
        Ok(str) => {
            state().set_cras_config_dir(&str);
        }
        Err(err) => {
            state().set_cras_config_dir("");
            log::info!("Failed to read cras-config-dir: {err}");
        }
    }
    if let Err(err) = state().read_beamforming_config() {
        log::error!("{err}");
    }
}

#[no_mangle]
pub extern "C" fn cras_s2_get_beamforming_supported() -> bool {
    state().output.get_bf_status().supported
}

#[no_mangle]
pub extern "C" fn cras_s2_get_beamforming_allowed() -> bool {
    state().output.get_bf_status().allowed
}

#[no_mangle]
pub extern "C" fn cras_s2_dump_json() -> *mut c_char {
    let s = serde_json::to_string_pretty(state().deref()).expect("serde_json::to_string_pretty");
    CString::new(s).expect("CString::new").into_raw()
}

#[no_mangle]
pub extern "C" fn cras_s2_reset_for_testing() {
    state().reset_for_testing();
}

#[no_mangle]
pub extern "C" fn cras_s2_set_nc_standalone_mode(nc_standalone_mode: bool) {
    state().set_nc_standalone_mode(nc_standalone_mode);
}

#[no_mangle]
pub extern "C" fn cras_s2_get_nc_standalone_mode() -> bool {
    state().input.nc_standalone_mode
}

#[no_mangle]
pub extern "C" fn cras_s2_set_non_dsp_aec_echo_ref_dev_alive(non_dsp_aec_echo_ref_dev_alive: bool) {
    state().set_non_dsp_aec_echo_ref_dev_alive(non_dsp_aec_echo_ref_dev_alive);
}

#[no_mangle]
pub extern "C" fn cras_s2_get_non_dsp_aec_echo_ref_dev_alive() -> bool {
    state().input.non_dsp_aec_echo_ref_dev_alive
}

#[no_mangle]
pub extern "C" fn cras_s2_set_aec_on_dsp_is_disallowed(aec_on_dsp_is_disallowed: bool) {
    state().set_aec_on_dsp_is_disallowed(aec_on_dsp_is_disallowed);
}

#[no_mangle]
pub extern "C" fn cras_s2_get_aec_on_dsp_is_disallowed() -> bool {
    state().input.aec_on_dsp_is_disallowed
}

#[no_mangle]
// Returns true for blocking DSP input effects; false for unblocking.
pub extern "C" fn cras_s2_get_dsp_input_effects_blocked() -> bool {
    state().output.dsp_input_effects_blocked
}

#[no_mangle]
pub extern "C" fn cras_s2_set_bypass_block_dsp_nc(bypass_block_dsp_nc: bool) {
    state().set_bypass_block_dsp_nc(bypass_block_dsp_nc);
}

#[no_mangle]
pub extern "C" fn cras_s2_get_bypass_block_dsp_nc() -> bool {
    state().input.bypass_block_dsp_nc
}

#[no_mangle]
pub extern "C" fn cras_s2_set_active_input_node_compatible_nc_providers(
    compatible_nc_providers: CRAS_NC_PROVIDER,
) {
    state().set_active_input_node_compatible_nc_providers(compatible_nc_providers);
}

#[no_mangle]
pub extern "C" fn cras_s2_get_cras_processor_effect(
    compatible_nc_providers: CRAS_NC_PROVIDER,
    client_controlled: bool,
    client_enabled: bool,
) -> CrasProcessorEffect {
    let enabled = if client_controlled {
        client_enabled
    } else {
        state().input.voice_isolation_ui_enabled
    };
    match state().resolve_nc_provider(compatible_nc_providers, enabled) {
        &CRAS_NC_PROVIDER::AP => CrasProcessorEffect::NoiseCancellation,
        &CRAS_NC_PROVIDER::DSP => CrasProcessorEffect::NoEffects,
        &CRAS_NC_PROVIDER::AST => CrasProcessorEffect::StyleTransfer,
        &CRAS_NC_PROVIDER::BF => CrasProcessorEffect::Beamforming,
        _ => CrasProcessorEffect::NoEffects,
    }
}

#[no_mangle]
pub extern "C" fn cras_s2_get_iodev_restart_tag_for_nc_providers(
    compatible_nc_providers: CRAS_NC_PROVIDER,
) -> CRAS_NC_PROVIDER {
    let enabled = state().input.voice_isolation_ui_enabled;
    *(state().resolve_nc_provider(compatible_nc_providers, enabled))
}

#[no_mangle]
pub extern "C" fn cras_s2_get_audio_effect_ui_appearance() -> CrasEffectUIAppearance {
    state().output.audio_effect_ui_appearance
}

#[no_mangle]
pub extern "C" fn cras_s2_set_spatial_audio_enabled(enabled: bool) {
    state().set_spatial_audio_enabled(enabled);
}

#[no_mangle]
pub extern "C" fn cras_s2_get_spatial_audio_enabled() -> bool {
    state().input.spatial_audio_enabled
}
