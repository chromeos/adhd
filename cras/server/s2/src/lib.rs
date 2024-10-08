// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use std::collections::HashMap;
use std::collections::HashSet;
use std::path::Path;

use anyhow::Context;
use audio_processor::cdcfg;
use cras_common::types_internal::CrasDlcId;
use cras_common::types_internal::CRAS_NC_PROVIDER;
use cras_common::types_internal::CRAS_NC_PROVIDER_PREFERENCE_ORDER;
use cras_common::types_internal::EFFECT_TYPE;
use serde::Serialize;

pub mod global;

pub const BEAMFORMING_CONFIG_PATH: &str = "/etc/cras/processor/beamforming.txtpb";

#[derive(Default, Serialize)]
struct Input {
    ap_nc_featured_allowed: bool,
    ap_nc_segmentation_allowed: bool,
    ap_nc_feature_tier_allowed: bool,
    /// Tells whether the DLC manager is ready.
    /// Used by tests to avoid races.
    dlc_manager_ready: bool,
    dlc_installed: HashSet<String>,
    dlc_manager_done: bool,
    style_transfer_featured_allowed: bool,
    // cros_config /audio/main cras-config-dir.
    cras_config_dir: String,
    // List of DLCs to provide the beamforming feature.
    // None means beamforming is not supported.
    beamforming_required_dlcs: Option<HashSet<String>>,
    active_input_node_compatible_nc_providers: CRAS_NC_PROVIDER,
    voice_isolation_ui_enabled: bool,

    /*
     * Flags to indicate that Noise Cancellation is blocked. Each flag handles own
     * scenario and will be updated in respective timing.
     *
     * 1. non_dsp_aec_echo_ref_dev_alive
     *     scenario:
     *         detect if there exists an enabled or opened output device which can't
     *         be applied as echo reference for AEC on DSP.
     *     timing for updating state:
     *         check rising edge on enable_dev() & open_dev() of output devices.
     *         check falling edge on disable_dev() & close_dev() of output devices.
     *
     * 2. aec_on_dsp_is_disallowed
     *     scenario:
     *         detect if there exists an input stream requesting AEC on DSP
     *         disallowed while it is supported.
     *     timing for updating state:
     *         check in stream_list_changed_cb() for all existing streams.
     *
     * The final NC blocking state is determined by:
     *     nc_blocked_state = (non_dsp_aec_echo_ref_dev_alive ||
     *                         aec_on_dsp_is_disallowed)
     *
     * CRAS will notify Chrome promptly when nc_blocked_state is altered.
     */
    non_dsp_aec_echo_ref_dev_alive: bool,
    aec_on_dsp_is_disallowed: bool,
    // TODO(b/236216566) remove this WA when DSP AEC is integrated on the only
    // NC-standalone case.
    // 1 - Noise Cancellation standalone mode, which implies that NC is
    // integrated without AEC on DSP. 0 - otherwise.
    nc_standalone_mode: bool,
    bypass_block_dsp_nc: bool,
    spatial_audio_enabled: bool,
}

#[derive(Serialize)]
struct Output {
    dsp_input_effects_blocked: bool,
    audio_effects_status: HashMap<CRAS_NC_PROVIDER, AudioEffectStatus>,
    nc_effect_for_ui_toggle: EFFECT_TYPE,
    // The NC providers that can be used if the node is compatible and the
    // effect should be enabled.
    system_valid_nc_providers: CRAS_NC_PROVIDER,
}

fn resolve(input: &Input) -> Output {
    // TODO(b/339785214): Decide this based on config file content.
    let beamforming_supported = input.cras_config_dir.ends_with(".3mic");
    let beamforming_allowed = match &input.beamforming_required_dlcs {
        None => false,
        Some(dlcs) => dlcs.iter().all(|dlc| input.dlc_installed.contains(dlc)),
    };
    let dlc_nc_ap_installed = input
        .dlc_installed
        .contains(CrasDlcId::CrasDlcNcAp.as_str());
    let dsp_input_effects_blocked = if input.bypass_block_dsp_nc {
        false
    } else if input.nc_standalone_mode {
        input.non_dsp_aec_echo_ref_dev_alive
    } else {
        input.non_dsp_aec_echo_ref_dev_alive || input.aec_on_dsp_is_disallowed
    };
    let audio_effects_status = HashMap::from([
        (
            CRAS_NC_PROVIDER::AP,
            AudioEffectStatus {
                supported: true,
                allowed: (input.ap_nc_featured_allowed
                    || input.ap_nc_segmentation_allowed
                    || input.ap_nc_feature_tier_allowed)
                    && dlc_nc_ap_installed,
                compatible_with_active_input_node: input
                    .active_input_node_compatible_nc_providers
                    .contains(CRAS_NC_PROVIDER::AP),
            },
        ),
        (
            CRAS_NC_PROVIDER::DSP,
            AudioEffectStatus {
                supported: true,
                allowed: !dsp_input_effects_blocked
                    // Prevent DSP NC from being selected by client controlled voice isolation.
                    && input.voice_isolation_ui_enabled,
                compatible_with_active_input_node: input
                    .active_input_node_compatible_nc_providers
                    .contains(CRAS_NC_PROVIDER::DSP),
            },
        ),
        (
            CRAS_NC_PROVIDER::AST,
            AudioEffectStatus {
                supported: input.ap_nc_segmentation_allowed && !beamforming_supported,
                allowed: input.style_transfer_featured_allowed && dlc_nc_ap_installed,
                compatible_with_active_input_node: input
                    .active_input_node_compatible_nc_providers
                    .contains(CRAS_NC_PROVIDER::AST),
            },
        ),
        (
            CRAS_NC_PROVIDER::BF,
            AudioEffectStatus {
                supported: beamforming_supported,
                allowed: beamforming_allowed,
                compatible_with_active_input_node: input
                    .active_input_node_compatible_nc_providers
                    .contains(CRAS_NC_PROVIDER::BF),
            },
        ),
    ]);
    let nc_effect_for_ui_toggle = resolve_effect_toggle_type(&audio_effects_status);
    // TODO(b/353627012): Set system_valid_nc_providers considering
    // nc_ui_selected_mode after the "Effect Mode options" in UI is implemented.
    let system_valid_nc_providers = audio_effects_status
        .iter()
        .filter_map(|(provider, status)| {
            if status.supported_and_allowed() {
                Some(*provider)
            } else {
                None
            }
        })
        .fold(CRAS_NC_PROVIDER::empty(), CRAS_NC_PROVIDER::union);

    Output {
        dsp_input_effects_blocked,
        audio_effects_status,
        nc_effect_for_ui_toggle,
        system_valid_nc_providers,
    }
}

fn resolve_effect_toggle_type(
    audio_effects_status: &HashMap<CRAS_NC_PROVIDER, AudioEffectStatus>,
) -> EFFECT_TYPE {
    if audio_effects_status
        .get(&CRAS_NC_PROVIDER::AST)
        .unwrap()
        .supported_and_allowed()
    // If incompatible with active input node, will still keep
    // the Style Transfer toggle, just the effect selection will fallback
    // to AP NC.
    {
        EFFECT_TYPE::STYLE_TRANSFER
    } else if audio_effects_status
        .get(&CRAS_NC_PROVIDER::BF)
        .unwrap()
        .supported_allowed_and_active_inode_compatible()
    {
        EFFECT_TYPE::BEAMFORMING
    } else if audio_effects_status
        .get(&CRAS_NC_PROVIDER::DSP)
        .unwrap()
        .supported_allowed_and_active_inode_compatible()
        || audio_effects_status
            .get(&CRAS_NC_PROVIDER::AP)
            .unwrap()
            .supported_allowed_and_active_inode_compatible()
    {
        EFFECT_TYPE::NOISE_CANCELLATION
    } else {
        EFFECT_TYPE::NONE
    }
}

impl Output {
    fn get_ap_nc_status(&self) -> &AudioEffectStatus {
        self.audio_effects_status
            .get(&CRAS_NC_PROVIDER::AP)
            .unwrap()
    }

    fn get_ast_status(&self) -> &AudioEffectStatus {
        self.audio_effects_status
            .get(&CRAS_NC_PROVIDER::AST)
            .unwrap()
    }

    fn get_bf_status(&self) -> &AudioEffectStatus {
        self.audio_effects_status
            .get(&CRAS_NC_PROVIDER::BF)
            .unwrap()
    }
}

#[derive(Default, Serialize)]
struct AudioEffectStatus {
    supported: bool,
    allowed: bool,
    compatible_with_active_input_node: bool,
}

impl AudioEffectStatus {
    #[cfg(test)]
    fn new_with_flag_value(value: bool) -> Self {
        AudioEffectStatus {
            supported: value,
            allowed: value,
            compatible_with_active_input_node: value,
        }
    }

    fn supported_and_allowed(&self) -> bool {
        self.supported && self.allowed
    }

    fn supported_allowed_and_active_inode_compatible(&self) -> bool {
        self.supported && self.allowed && self.compatible_with_active_input_node
    }
}

#[derive(Serialize)]
struct S2 {
    input: Input,
    output: Output,
}

impl S2 {
    fn new() -> Self {
        let input = Default::default();
        let output = resolve(&input);
        Self { input, output }
    }

    fn set_ap_nc_featured_allowed(&mut self, allowed: bool) {
        self.input.ap_nc_featured_allowed = allowed;
        self.update();
    }

    fn set_ap_nc_segmentation_allowed(&mut self, allowed: bool) {
        self.input.ap_nc_segmentation_allowed = allowed;
        self.update();
    }

    fn set_ap_nc_feature_tier_allowed(&mut self, allowed: bool) {
        self.input.ap_nc_feature_tier_allowed = allowed;
        self.update();
    }

    fn set_dlc_manager_ready(&mut self) {
        self.input.dlc_manager_ready = true;
        self.update();
    }

    fn set_dlc_installed(&mut self, dlc: CrasDlcId) {
        self.input.dlc_installed.insert(dlc.as_str().to_string());
        self.update();
    }

    fn set_dlc_manager_done(&mut self) {
        self.input.dlc_manager_done = true;
        self.update()
    }

    fn set_style_transfer_featured_allowed(&mut self, allowed: bool) {
        self.input.style_transfer_featured_allowed = allowed;
        self.update();
    }

    fn set_voice_isolation_ui_enabled(&mut self, enabled: bool) {
        self.input.voice_isolation_ui_enabled = enabled;
        self.update();
    }

    fn set_cras_config_dir(&mut self, cras_config_dir: &str) {
        self.input.cras_config_dir = cras_config_dir.into();
        self.update();
    }

    fn set_beamforming_required_dlcs(&mut self, dlcs: HashSet<String>) {
        self.input.beamforming_required_dlcs = Some(dlcs);
        self.update();
    }

    fn set_active_input_node_compatible_nc_providers(
        &mut self,
        compatible_nc_providers: CRAS_NC_PROVIDER,
    ) {
        self.input.active_input_node_compatible_nc_providers = compatible_nc_providers;
        self.update();
    }

    fn set_nc_standalone_mode(&mut self, nc_standalone_mode: bool) {
        self.input.nc_standalone_mode = nc_standalone_mode;
        self.update();
    }

    fn set_non_dsp_aec_echo_ref_dev_alive(&mut self, non_dsp_aec_echo_ref_dev_alive: bool) {
        self.input.non_dsp_aec_echo_ref_dev_alive = non_dsp_aec_echo_ref_dev_alive;
        self.update();
    }

    fn set_aec_on_dsp_is_disallowed(&mut self, aec_on_dsp_is_disallowed: bool) {
        self.input.aec_on_dsp_is_disallowed = aec_on_dsp_is_disallowed;
        self.update();
    }

    fn set_bypass_block_dsp_nc(&mut self, bypass_block_dsp_nc: bool) {
        self.input.bypass_block_dsp_nc = bypass_block_dsp_nc;
        self.update();
    }

    fn resolve_nc_provider(
        &self,
        compatible_nc_providers: CRAS_NC_PROVIDER,
        enabled: bool,
    ) -> &CRAS_NC_PROVIDER {
        if !enabled {
            return &CRAS_NC_PROVIDER::NONE;
        }
        let valid_nc_providers = compatible_nc_providers & self.output.system_valid_nc_providers;
        for nc_provider in CRAS_NC_PROVIDER_PREFERENCE_ORDER {
            if valid_nc_providers.contains(*nc_provider) {
                return nc_provider;
            }
        }
        &CRAS_NC_PROVIDER::NONE
    }

    fn read_beamforming_config(&mut self) -> anyhow::Result<()> {
        self.set_beamforming_required_dlcs(
            cdcfg::get_required_dlcs(Path::new(BEAMFORMING_CONFIG_PATH))
                .context("get_required_dlcs")?,
        );
        Ok(())
    }

    fn set_spatial_audio_enabled(&mut self, enabled: bool) {
        self.input.spatial_audio_enabled = enabled;
        self.update();
    }

    fn update(&mut self) {
        self.output = resolve(&self.input);
    }

    fn reset_for_testing(&mut self) {
        *self = Self::new();
    }
}

#[cfg(test)]
mod tests {
    use std::collections::HashMap;
    use std::collections::HashSet;

    use cras_common::types_internal::CrasDlcId;
    use cras_common::types_internal::CRAS_NC_PROVIDER;
    use cras_common::types_internal::EFFECT_TYPE;

    use crate::resolve_effect_toggle_type;
    use crate::AudioEffectStatus;
    use crate::S2;

    #[test]
    fn test_ap_nc() {
        let mut s = S2::new();
        assert_eq!(s.output.get_ap_nc_status().allowed, false);

        s.set_ap_nc_featured_allowed(true);
        assert_eq!(s.output.get_ap_nc_status().allowed, false);

        s.set_ap_nc_featured_allowed(false);
        s.set_ap_nc_segmentation_allowed(true);
        assert_eq!(s.output.get_ap_nc_status().allowed, false);

        s.set_ap_nc_segmentation_allowed(false);
        s.set_ap_nc_feature_tier_allowed(true);
        assert_eq!(s.output.get_ap_nc_status().allowed, false);

        s.set_dlc_installed(CrasDlcId::CrasDlcNcAp);

        s.set_ap_nc_featured_allowed(true);
        assert_eq!(s.output.get_ap_nc_status().allowed, true);

        s.set_ap_nc_featured_allowed(false);
        s.set_ap_nc_segmentation_allowed(true);
        assert_eq!(s.output.get_ap_nc_status().allowed, true);

        s.set_ap_nc_segmentation_allowed(false);
        s.set_ap_nc_feature_tier_allowed(true);
        assert_eq!(s.output.get_ap_nc_status().allowed, true);
    }

    #[test]
    fn test_style_transfer_supported() {
        let mut s = S2::new();
        assert_eq!(s.output.get_ast_status().supported, false);

        s.set_ap_nc_segmentation_allowed(true);
        assert_eq!(s.output.get_ast_status().supported, true);

        s.set_cras_config_dir("omniknight.3mic");
        assert_eq!(s.output.get_ast_status().supported, false);
    }

    #[test]
    fn test_style_transfer_allowed() {
        let mut s = S2::new();
        assert_eq!(s.output.get_ast_status().allowed, false);

        s.set_style_transfer_featured_allowed(true);
        assert_eq!(s.output.get_ast_status().allowed, false);

        s.set_dlc_installed(CrasDlcId::CrasDlcNcAp);
        assert_eq!(s.output.get_ast_status().allowed, true);

        s.set_style_transfer_featured_allowed(false);
        assert_eq!(s.output.get_ast_status().allowed, false);
    }

    #[test]
    fn test_voice_isolation_ui_enabled() {
        let mut s = S2::new();
        assert_eq!(s.input.voice_isolation_ui_enabled, false);

        s.set_voice_isolation_ui_enabled(true);
        assert_eq!(s.input.voice_isolation_ui_enabled, true);

        s.set_voice_isolation_ui_enabled(false);
        assert_eq!(s.input.voice_isolation_ui_enabled, false);
    }

    #[test]
    fn test_beamforming() {
        let mut s = S2::new();
        s.set_ap_nc_segmentation_allowed(true);
        s.set_style_transfer_featured_allowed(true);
        s.set_beamforming_required_dlcs(HashSet::from([CrasDlcId::CrasDlcIntelligoBeamforming
            .as_str()
            .to_string()]));
        assert!(!s.output.get_bf_status().supported);
        assert!(s.output.get_ast_status().supported);

        s.set_cras_config_dir("omniknight.3mic");
        assert!(s.output.get_bf_status().supported);
        assert!(!s.output.get_ast_status().supported);

        assert!(!s.output.get_bf_status().allowed);
        s.set_dlc_installed(CrasDlcId::CrasDlcIntelligoBeamforming);
        assert!(s.output.get_bf_status().allowed);

        let dlcs = HashSet::from(["does-not-exist".to_string()]);
        s.set_beamforming_required_dlcs(dlcs);
        assert!(!s.output.get_bf_status().allowed);
        s.input.beamforming_required_dlcs = None;
        s.update();
        assert!(!s.output.get_bf_status().allowed);

        s.set_cras_config_dir("omniknight");
        assert!(!s.output.get_bf_status().supported);
        assert!(s.output.get_ast_status().supported);
    }

    #[test]
    fn test_dlc() {
        let mut s = S2::new();
        assert!(!s.input.dlc_manager_ready);
        assert!(s.input.dlc_installed.is_empty());
        assert!(!s.input.dlc_manager_done);

        s.set_dlc_manager_ready();
        assert!(s.input.dlc_manager_ready);

        s.set_dlc_installed(CrasDlcId::CrasDlcNcAp);
        assert_eq!(
            s.input.dlc_installed,
            HashSet::from([CrasDlcId::CrasDlcNcAp.as_str().to_string()])
        );
        s.set_dlc_installed(CrasDlcId::CrasDlcIntelligoBeamforming);
        assert_eq!(
            s.input.dlc_installed,
            HashSet::from([
                CrasDlcId::CrasDlcNcAp.as_str().to_string(),
                CrasDlcId::CrasDlcIntelligoBeamforming.as_str().to_string()
            ])
        );

        s.set_dlc_manager_done();
        assert!(s.input.dlc_manager_done);
    }

    #[test]
    fn test_dsp_blocking() {
        let mut s = S2::new();
        assert_eq!(s.input.nc_standalone_mode, false);
        assert_eq!(s.input.non_dsp_aec_echo_ref_dev_alive, false);
        assert_eq!(s.input.aec_on_dsp_is_disallowed, false);
        assert_eq!(s.output.dsp_input_effects_blocked, false);

        s.set_non_dsp_aec_echo_ref_dev_alive(true);
        assert_eq!(s.input.non_dsp_aec_echo_ref_dev_alive, true);
        assert_eq!(s.output.dsp_input_effects_blocked, true);
        s.set_non_dsp_aec_echo_ref_dev_alive(false);
        assert_eq!(s.input.non_dsp_aec_echo_ref_dev_alive, false);
        assert_eq!(s.output.dsp_input_effects_blocked, false);

        s.set_aec_on_dsp_is_disallowed(true);
        assert_eq!(s.input.aec_on_dsp_is_disallowed, true);
        assert_eq!(s.output.dsp_input_effects_blocked, true);

        // NC standalone mode will ignore aec_on_dsp_is_disallowed
        s.set_nc_standalone_mode(true);
        assert_eq!(s.input.nc_standalone_mode, true);
        assert_eq!(s.output.dsp_input_effects_blocked, false);
        s.set_nc_standalone_mode(false);
        assert_eq!(s.input.nc_standalone_mode, false);
        assert_eq!(s.output.dsp_input_effects_blocked, true);

        s.set_aec_on_dsp_is_disallowed(false);
        assert_eq!(s.input.aec_on_dsp_is_disallowed, false);
        assert_eq!(s.output.dsp_input_effects_blocked, false);
    }

    #[test]
    fn test_bypass_block_dsp_nc() {
        let mut s = S2::new();
        assert_eq!(s.input.bypass_block_dsp_nc, false);

        s.set_aec_on_dsp_is_disallowed(true);
        assert_eq!(s.output.dsp_input_effects_blocked, true);
        s.set_bypass_block_dsp_nc(true);
        assert_eq!(s.input.bypass_block_dsp_nc, true);
        assert_eq!(s.output.dsp_input_effects_blocked, false);
        s.set_bypass_block_dsp_nc(false);
        assert_eq!(s.input.bypass_block_dsp_nc, false);
        assert_eq!(s.output.dsp_input_effects_blocked, true);
    }

    #[test]
    fn test_resolve_effect_toggle_type() {
        let mut audio_effects_status = HashMap::from([
            (CRAS_NC_PROVIDER::AP, AudioEffectStatus::default()),
            (CRAS_NC_PROVIDER::DSP, AudioEffectStatus::default()),
            (CRAS_NC_PROVIDER::AST, AudioEffectStatus::default()),
            (CRAS_NC_PROVIDER::BF, AudioEffectStatus::default()),
        ]);
        assert_eq!(
            resolve_effect_toggle_type(&audio_effects_status),
            EFFECT_TYPE::NONE
        );

        audio_effects_status.insert(
            CRAS_NC_PROVIDER::AP,
            AudioEffectStatus::new_with_flag_value(true),
        );
        assert_eq!(
            resolve_effect_toggle_type(&audio_effects_status),
            EFFECT_TYPE::NOISE_CANCELLATION
        );
        audio_effects_status.insert(
            CRAS_NC_PROVIDER::AP,
            AudioEffectStatus::new_with_flag_value(false),
        );
        assert_eq!(
            resolve_effect_toggle_type(&audio_effects_status),
            EFFECT_TYPE::NONE
        );
        audio_effects_status.insert(
            CRAS_NC_PROVIDER::DSP,
            AudioEffectStatus::new_with_flag_value(true),
        );
        assert_eq!(
            resolve_effect_toggle_type(&audio_effects_status),
            EFFECT_TYPE::NOISE_CANCELLATION
        );

        audio_effects_status.insert(
            CRAS_NC_PROVIDER::BF,
            AudioEffectStatus::new_with_flag_value(true),
        );
        assert_eq!(
            resolve_effect_toggle_type(&audio_effects_status),
            EFFECT_TYPE::BEAMFORMING
        );

        audio_effects_status.insert(
            CRAS_NC_PROVIDER::AST,
            AudioEffectStatus::new_with_flag_value(true),
        );
        assert_eq!(
            resolve_effect_toggle_type(&audio_effects_status),
            EFFECT_TYPE::STYLE_TRANSFER
        );
    }

    #[test]
    fn test_system_valid_nc_providers() {
        // TODO(b/353627012): Set system_valid_nc_providers considering
        // nc_ui_selected_mode after the "Effect Mode options" in UI is implemented.

        let mut s = S2::new();
        let mut expected = CRAS_NC_PROVIDER::NONE;
        assert_eq!(s.output.system_valid_nc_providers, expected);

        s.set_voice_isolation_ui_enabled(true);
        expected |= CRAS_NC_PROVIDER::DSP;
        assert_eq!(s.output.system_valid_nc_providers, expected);

        s.set_ap_nc_featured_allowed(true);
        s.set_dlc_installed(CrasDlcId::CrasDlcNcAp);
        expected |= CRAS_NC_PROVIDER::AP;
        assert_eq!(s.output.system_valid_nc_providers, expected);

        s.set_ap_nc_segmentation_allowed(true);
        s.set_style_transfer_featured_allowed(true);
        expected |= CRAS_NC_PROVIDER::AST;
        assert_eq!(s.output.system_valid_nc_providers, expected);

        s.set_beamforming_required_dlcs(HashSet::from([CrasDlcId::CrasDlcIntelligoBeamforming
            .as_str()
            .to_string()]));
        s.set_dlc_installed(CrasDlcId::CrasDlcIntelligoBeamforming);
        s.set_cras_config_dir("omniknight.3mic");
        expected |= CRAS_NC_PROVIDER::BF;
        // TODO(b/353627012): Currently BF and AST are mutually exclusive. Update test when they're not.
        expected.remove(CRAS_NC_PROVIDER::AST);
        assert_eq!(s.output.system_valid_nc_providers, expected);
    }

    #[test]
    fn test_resolve_nc_provider() {
        // TODO(b/353627012): Update test for Beamforming after "effect mode" selection is ready.
        let mut s = S2::new();
        s.set_voice_isolation_ui_enabled(true);

        // Set AP NC supported and allowed.
        s.set_ap_nc_featured_allowed(true);
        s.set_dlc_installed(CrasDlcId::CrasDlcNcAp);
        // DSP NC priority is higher than AP NC.
        assert_eq!(
            s.resolve_nc_provider(CRAS_NC_PROVIDER::all(), true),
            &CRAS_NC_PROVIDER::DSP
        );
        s.set_non_dsp_aec_echo_ref_dev_alive(true);
        assert_eq!(
            s.resolve_nc_provider(CRAS_NC_PROVIDER::all(), true),
            &CRAS_NC_PROVIDER::AP
        );
        s.set_non_dsp_aec_echo_ref_dev_alive(false);

        // Set AST supported and allowed.
        s.set_ap_nc_segmentation_allowed(true);
        s.set_style_transfer_featured_allowed(true);
        assert_eq!(
            s.resolve_nc_provider(CRAS_NC_PROVIDER::all(), true),
            &CRAS_NC_PROVIDER::AST
        );

        // Test for effect disabled.
        for i in 0..CRAS_NC_PROVIDER::all().bits() {
            let compatible_nc_providers = CRAS_NC_PROVIDER::from_bits_truncate(i);
            assert_eq!(
                s.resolve_nc_provider(compatible_nc_providers, false),
                &CRAS_NC_PROVIDER::NONE
            );
        }

        // Test for different sets of compatible NC providers.
        let mut compatible = CRAS_NC_PROVIDER::NONE;
        assert_eq!(
            s.resolve_nc_provider(compatible, true),
            &CRAS_NC_PROVIDER::NONE
        );
        compatible |= CRAS_NC_PROVIDER::AP;
        assert_eq!(
            s.resolve_nc_provider(compatible, true),
            &CRAS_NC_PROVIDER::AP
        );
        compatible |= CRAS_NC_PROVIDER::DSP;
        assert_eq!(
            s.resolve_nc_provider(compatible, true),
            &CRAS_NC_PROVIDER::DSP
        );
        compatible |= CRAS_NC_PROVIDER::AST;
        assert_eq!(
            s.resolve_nc_provider(compatible, true),
            &CRAS_NC_PROVIDER::AST
        );
    }

    #[test]
    fn test_spatial_audio_enabled() {
        let mut s = S2::new();
        assert_eq!(s.input.spatial_audio_enabled, false);

        s.set_spatial_audio_enabled(true);
        assert_eq!(s.input.spatial_audio_enabled, true);

        s.set_spatial_audio_enabled(false);
        assert_eq!(s.input.spatial_audio_enabled, false);
    }
}
