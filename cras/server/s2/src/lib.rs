// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use std::collections::HashMap;
use std::collections::HashSet;
use std::path::Path;

use cras_common::types_internal::CrasEffectUIAppearance;
use cras_common::types_internal::CRAS_NC_PROVIDER;
use cras_common::types_internal::CRAS_NC_PROVIDER_PREFERENCE_ORDER;
use cras_common::types_internal::EFFECT_TYPE;
use cras_common::types_internal::NC_AP_DLC;
use cras_common::types_internal::SR_BT_DLC;
use cras_feature_tier::CrasFeatureTier;
use cras_ini::CrasIniMap;
use global::ResetIodevListForVoiceIsolation;
use probe::probe_board_name;
use probe::probe_cpu_model_name;
use processing::BeamformingConfig;
use serde::Serialize;

use crate::global::NotifyAudioEffectUIAppearanceChanged;

pub mod global;
mod probe;
pub mod processing;

#[derive(Default, Serialize)]
struct Input {
    feature_tier: CrasFeatureTier,
    board_name: String,
    cpu_model_name: String,
    ap_nc_featured_allowed: bool,
    ap_nc_segmentation_allowed: bool,
    /// Tells whether the DLC manager is ready.
    /// Used by tests to avoid races.
    dlc_manager_ready: bool,
    /// Cached result of compute_dlcs_to_install.
    /// None if not initialized yet.
    dlcs_to_install_cached: Option<Vec<String>>,
    dlcs_installed: HashSet<String>,
    style_transfer_featured_allowed: bool,
    // cros_config /audio/main cras-config-dir.
    cras_config_dir: String,
    cras_processor_vars: HashMap<String, String>,
    beamforming_config: BeamformingConfig,
    active_input_node_compatible_nc_providers: CRAS_NC_PROVIDER,
    voice_isolation_ui_enabled: bool,
    voice_isolation_ui_preferred_effect: EFFECT_TYPE,
    dsp_nc_supported: bool,

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
    spatial_audio_supported: bool,
}

#[derive(Serialize)]
pub struct Output {
    audio_effects_ready: bool,
    dsp_input_effects_blocked: bool,
    audio_effects_status: HashMap<CRAS_NC_PROVIDER, AudioEffectStatus>,
    audio_effect_ui_appearance: CrasEffectUIAppearance,
    // The NC providers that can be used if the node is compatible and the
    // effect should be enabled.
    system_valid_nc_providers: CRAS_NC_PROVIDER,
    // Added voice_isolation_ui_enabled here to track the value change in S2::update().
    voice_isolation_ui_enabled: bool,
    pub label_audio_beamforming: &'static str,
}

fn resolve(input: &Input) -> Output {
    // TODO(b/339785214): Decide this based on config file content.
    let beamforming_supported = matches!(input.beamforming_config, BeamformingConfig::Supported(_));
    let beamforming_allowed = match &input.beamforming_config {
        BeamformingConfig::Unsupported { .. } => false,
        BeamformingConfig::Supported(properties) => {
            properties.required_dlcs.is_subset(&input.dlcs_installed)
        }
    };
    let dlc_nc_ap_installed = input.dlcs_installed.contains(NC_AP_DLC);
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
                    || input.feature_tier.ap_nc_supported)
                    && dlc_nc_ap_installed,
                compatible_with_active_input_node: input
                    .active_input_node_compatible_nc_providers
                    .contains(CRAS_NC_PROVIDER::AP),
            },
        ),
        (
            CRAS_NC_PROVIDER::DSP,
            AudioEffectStatus {
                supported: input.dsp_nc_supported,
                allowed: !dsp_input_effects_blocked,
                compatible_with_active_input_node: input
                    .active_input_node_compatible_nc_providers
                    .contains(CRAS_NC_PROVIDER::DSP),
            },
        ),
        (
            CRAS_NC_PROVIDER::AST,
            AudioEffectStatus {
                supported: input.ap_nc_segmentation_allowed,
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
    let audio_effect_ui_appearance = CrasEffectUIAppearance {
        toggle_type: nc_effect_for_ui_toggle,
        effect_mode_options: if audio_effects_status
            .get(&CRAS_NC_PROVIDER::AST)
            .unwrap()
            .supported_and_allowed()
            && audio_effects_status
                .get(&CRAS_NC_PROVIDER::BF)
                .unwrap()
                .supported_and_allowed()
        {
            EFFECT_TYPE::STYLE_TRANSFER | EFFECT_TYPE::BEAMFORMING
        } else {
            EFFECT_TYPE::NONE
        },
        show_effect_fallback_message: match nc_effect_for_ui_toggle {
            EFFECT_TYPE::STYLE_TRANSFER => {
                !audio_effects_status
                    .get(&CRAS_NC_PROVIDER::AST)
                    .unwrap()
                    .compatible_with_active_input_node
            }
            EFFECT_TYPE::BEAMFORMING => {
                !audio_effects_status
                    .get(&CRAS_NC_PROVIDER::BF)
                    .unwrap()
                    .compatible_with_active_input_node
            }
            _ => false,
        },
    };

    let system_valid_nc_providers = audio_effects_status
        .iter()
        .filter_map(|(provider, status)| {
            if status.supported_and_allowed() {
                Some(*provider)
            } else {
                None
            }
        })
        .fold(CRAS_NC_PROVIDER::empty(), CRAS_NC_PROVIDER::union)
        & input.voice_isolation_ui_preferred_effect.nc_providers();

    Output {
        audio_effects_ready: input
            .dlcs_to_install_cached
            .as_ref()
            .is_some_and(|dlcs_to_install| {
                dlcs_to_install
                    .iter()
                    .all(|dlc| input.dlcs_installed.contains(dlc))
            }),
        dsp_input_effects_blocked,
        audio_effects_status,
        audio_effect_ui_appearance,
        system_valid_nc_providers,
        voice_isolation_ui_enabled: input.voice_isolation_ui_enabled,
        label_audio_beamforming: match input.beamforming_config {
            BeamformingConfig::Supported(_) => "intelligo",
            BeamformingConfig::Unsupported { .. } => "none",
        },
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

#[derive(Default)]
struct CrasS2Callbacks {
    notify_audio_effect_ui_appearance_changed: Option<NotifyAudioEffectUIAppearanceChanged>,
    reset_iodev_list_for_voice_isolation: Option<ResetIodevListForVoiceIsolation>,
}

#[derive(Serialize)]
pub struct S2 {
    input: Input,
    pub output: Output,

    #[serde(skip)]
    callbacks: CrasS2Callbacks,
}

impl S2 {
    pub fn new() -> Self {
        let input = Default::default();
        let output = resolve(&input);
        Self {
            input,
            output,
            callbacks: Default::default(),
        }
    }

    fn compute_dlcs_to_install(&self) -> Vec<String> {
        let mut out = Vec::new();
        if self.input.feature_tier.sr_bt_supported {
            out.push(SR_BT_DLC.to_string());
        }
        if self.output.get_ap_nc_status().supported {
            out.push(NC_AP_DLC.to_string());
        }
        if let BeamformingConfig::Supported(properties) = &self.input.beamforming_config {
            out.extend(properties.required_dlcs.iter().cloned());
        }
        out
    }

    fn set_feature_tier(&mut self, feature_tier: CrasFeatureTier) {
        self.input.feature_tier = feature_tier;
        self.update();
    }

    fn set_ap_nc_featured_allowed(&mut self, allowed: bool) {
        self.input.ap_nc_featured_allowed = allowed;
        self.update();
    }

    fn set_ap_nc_segmentation_allowed(&mut self, allowed: bool) {
        self.input.ap_nc_segmentation_allowed = allowed;
        self.update();
    }

    fn set_dlc_manager_ready(&mut self) {
        self.input.dlc_manager_ready = true;
        self.update();
    }

    fn set_dlc_installed(&mut self, dlc: &str, installed: bool) {
        if installed {
            self.input.dlcs_installed.insert(dlc.to_string());
        } else {
            self.input.dlcs_installed.remove(dlc);
        }
        self.update();
    }

    fn set_style_transfer_featured_allowed(&mut self, allowed: bool) {
        self.input.style_transfer_featured_allowed = allowed;
        self.update();
    }

    fn set_voice_isolation_ui_enabled(&mut self, enabled: bool) {
        self.input.voice_isolation_ui_enabled = enabled;
        self.update();
    }

    fn set_voice_isolation_ui_preferred_effect(&mut self, preferred_effect: EFFECT_TYPE) {
        self.input.voice_isolation_ui_preferred_effect = preferred_effect;
        self.update();
    }

    /// Initialize the instance by reading from system configs and the environment.
    fn init(&mut self) {
        let board_name = match probe_board_name() {
            Ok(name) => name,
            Err(err) => {
                log::warn!("Failed to get board name: {err:#}");
                String::new()
            }
        };
        let cpu_model_name = match probe_cpu_model_name() {
            Ok(name) => name,
            Err(err) => {
                log::warn!("Failed to get CPU model name: {err:#}");
                String::new()
            }
        };
        self.init_with_board_and_cpu(board_name, cpu_model_name);
    }

    fn init_with_board_and_cpu(&mut self, board_name: String, cpu_model_name: String) {
        self.input.board_name = board_name;
        self.input.cpu_model_name = cpu_model_name;
        self.read_cras_config();
        self.set_feature_tier(CrasFeatureTier::new(
            &self.input.board_name,
            &self.input.cpu_model_name,
        ));
        self.input.dlcs_to_install_cached = Some(self.compute_dlcs_to_install());
        self.update();
    }

    pub fn read_cras_config(&mut self) {
        match std::fs::read_to_string("/run/chromeos-config/v1/audio/main/cras-config-dir") {
            Ok(str) => {
                self.read_cras_config_from_dir(&str);
            }
            Err(err) => {
                self.read_cras_config_from_dir("");
                log::error!("Failed to read cras-config-dir: {err:#}");
            }
        }
    }

    fn read_cras_config_from_dir(&mut self, cras_config_dir: &str) {
        self.input.cras_config_dir = cras_config_dir.into();
        let board_ini = match cras_ini::parse_file(
            &Path::new("/etc/cras")
                .join(cras_config_dir)
                .join("board.ini"),
        ) {
            Ok(map) => map,
            Err(err) => {
                log::error!("cannot parse board.ini {err:#}");
                CrasIniMap::default()
            }
        };
        self.input.cras_processor_vars = match board_ini.get("cras_processor_vars") {
            Some(vars) => {
                let x = HashMap::from_iter(
                    vars.iter()
                        .map(|(k, v)| (k.as_str().to_string(), v.as_str().to_string())),
                );
                x
            }
            None => HashMap::new(),
        };
        self.input.beamforming_config =
            BeamformingConfig::probe(&board_ini, &self.input.cras_processor_vars);
        self.update();
    }

    #[cfg(test)]
    fn set_beamforming_config(&mut self, beamforming_config: BeamformingConfig) {
        self.input.beamforming_config = beamforming_config;
        self.update();
    }

    fn set_active_input_node_compatible_nc_providers(
        &mut self,
        compatible_nc_providers: CRAS_NC_PROVIDER,
    ) {
        self.input.active_input_node_compatible_nc_providers = compatible_nc_providers;
        self.update();
    }

    fn set_dsp_nc_supported(&mut self, dsp_nc_supported: bool) {
        self.input.dsp_nc_supported = dsp_nc_supported;
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
        self.resolve_nc_provider_with_client_controlled_voice_isolation(
            compatible_nc_providers,
            enabled,
            false,
        )
    }

    fn resolve_nc_provider_with_client_controlled_voice_isolation(
        &self,
        compatible_nc_providers: CRAS_NC_PROVIDER,
        enabled: bool,
        client_controlled_voice_isolation: bool,
    ) -> &CRAS_NC_PROVIDER {
        if !enabled {
            return &CRAS_NC_PROVIDER::NONE;
        }
        let mut valid_nc_providers =
            compatible_nc_providers & self.output.system_valid_nc_providers;
        if client_controlled_voice_isolation && !self.input.voice_isolation_ui_enabled {
            // TODO(b/376794387): Allow DSP NC to be selected by client controlled voice isolation.
            // Prevent DSP NC from being selected by client controlled voice isolation.
            valid_nc_providers.remove(CRAS_NC_PROVIDER::DSP);
        }
        for nc_provider in CRAS_NC_PROVIDER_PREFERENCE_ORDER {
            if valid_nc_providers.contains(*nc_provider) {
                return nc_provider;
            }
        }
        &CRAS_NC_PROVIDER::NONE
    }

    fn set_spatial_audio_enabled(&mut self, enabled: bool) {
        self.input.spatial_audio_enabled = enabled;
        self.update();
    }

    fn set_spatial_audio_supported(&mut self, supported: bool) {
        self.input.spatial_audio_supported = supported;
        self.update();
    }

    fn set_notify_audio_effect_ui_appearance_changed(
        &mut self,
        notify_audio_effect_ui_appearance_changed: NotifyAudioEffectUIAppearanceChanged,
    ) {
        self.callbacks.notify_audio_effect_ui_appearance_changed =
            Some(notify_audio_effect_ui_appearance_changed);
    }

    fn set_reset_iodev_list_for_voice_isolation(
        &mut self,
        reset_iodev_list_for_voice_isolation: ResetIodevListForVoiceIsolation,
    ) {
        self.callbacks.reset_iodev_list_for_voice_isolation =
            Some(reset_iodev_list_for_voice_isolation);
    }

    fn update(&mut self) {
        let next_output = resolve(&self.input);

        if let Some(callback) = self.callbacks.notify_audio_effect_ui_appearance_changed {
            if next_output.audio_effects_ready != self.output.audio_effects_ready
                || next_output.audio_effect_ui_appearance != self.output.audio_effect_ui_appearance
            {
                callback(next_output.audio_effect_ui_appearance)
            }
        }
        if next_output.system_valid_nc_providers != self.output.system_valid_nc_providers
            || next_output.voice_isolation_ui_enabled != self.output.voice_isolation_ui_enabled
        {
            if let Some(callback) = self.callbacks.reset_iodev_list_for_voice_isolation {
                callback()
            }
        }

        self.output = next_output;
    }

    fn reset_for_testing(&mut self) {
        *self = Self::new();
    }
}

#[cfg(test)]
mod tests {
    use std::collections::HashMap;
    use std::collections::HashSet;
    use std::sync::atomic::AtomicBool;
    use std::sync::atomic::AtomicU32;
    use std::sync::atomic::Ordering;

    use cras_common::types_internal::CrasEffectUIAppearance;
    use cras_common::types_internal::CRAS_NC_PROVIDER;
    use cras_common::types_internal::EFFECT_TYPE;
    use cras_common::types_internal::NC_AP_DLC;

    use crate::processing::BeamformingConfig;
    use crate::processing::BeamformingProperties;
    use crate::resolve_effect_toggle_type;
    use crate::AudioEffectStatus;
    use crate::S2;

    const BF_DLC: &str = "bf-dlc-for-test";

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
        s.input.feature_tier.ap_nc_supported = true;
        assert_eq!(s.output.get_ap_nc_status().allowed, false);

        s.set_dlc_installed(NC_AP_DLC, true);

        s.set_ap_nc_featured_allowed(true);
        assert_eq!(s.output.get_ap_nc_status().allowed, true);

        s.set_ap_nc_featured_allowed(false);
        s.set_ap_nc_segmentation_allowed(true);
        assert_eq!(s.output.get_ap_nc_status().allowed, true);

        s.set_ap_nc_segmentation_allowed(false);
        s.input.feature_tier.ap_nc_supported = true;
        assert_eq!(s.output.get_ap_nc_status().allowed, true);
    }

    #[test]
    fn test_style_transfer_supported() {
        let mut s = S2::new();
        assert_eq!(s.output.get_ast_status().supported, false);

        s.set_ap_nc_segmentation_allowed(true);
        assert_eq!(s.output.get_ast_status().supported, true);
    }

    #[test]
    fn test_style_transfer_allowed() {
        let mut s = S2::new();
        assert_eq!(s.output.get_ast_status().allowed, false);

        s.set_style_transfer_featured_allowed(true);
        assert_eq!(s.output.get_ast_status().allowed, false);

        s.set_dlc_installed(NC_AP_DLC, true);
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
        s.set_beamforming_config(BeamformingConfig::Supported(BeamformingProperties {
            required_dlcs: HashSet::from([BF_DLC.to_string()]),
            ..Default::default()
        }));
        assert!(s.output.get_bf_status().supported);
        assert!(s.output.get_ast_status().supported);

        s.input.cras_config_dir = "does not end with .3mic____".to_string(); // The config dir should not matter.
        s.update();
        assert!(s.output.get_bf_status().supported);

        assert!(!s.output.get_bf_status().allowed);
        s.set_dlc_installed(BF_DLC, true);
        assert!(s.output.get_bf_status().allowed);

        let dlcs = HashSet::from(["does-not-exist".to_string()]);
        s.set_beamforming_config(BeamformingConfig::Supported(BeamformingProperties {
            required_dlcs: dlcs,
            ..Default::default()
        }));
        assert!(!s.output.get_bf_status().allowed);
        s.set_beamforming_config(BeamformingConfig::Unsupported {
            reason: "testing".to_string(),
        });
        s.update();
        assert!(!s.output.get_bf_status().allowed);
    }

    #[test]
    fn test_dlc() {
        let mut s = S2::new();
        assert!(s.input.dlcs_installed.is_empty());
        assert!(!s.input.dlc_manager_ready);
        s.set_dlc_manager_ready();
        assert!(s.input.dlc_manager_ready);

        s.set_dlc_installed(NC_AP_DLC, true);
        assert_eq!(
            s.input.dlcs_installed,
            HashSet::from([NC_AP_DLC.to_string()])
        );

        s.set_dlc_installed(NC_AP_DLC, false);
        assert_eq!(s.input.dlcs_installed, HashSet::new());
    }

    #[test]
    fn test_audio_effects_ready() {
        let mut s = S2::new();
        assert!(!s.output.audio_effects_ready);

        s.input.dlcs_to_install_cached = Some(vec![NC_AP_DLC.to_string(), BF_DLC.to_string()]);
        s.update();
        assert!(
            !s.output.audio_effects_ready,
            "{}",
            serde_json::to_string_pretty(&s).unwrap()
        );

        // Simply verifies the callback is called.
        static CALLED: AtomicBool = AtomicBool::new(false);
        static TOGGLE_TYPE: AtomicU32 = AtomicU32::new(0);
        static EFFECT_MODE_OPTIONS: AtomicU32 = AtomicU32::new(0);
        static SHOW_EFFECT_FALLBACK_MESSAGE: AtomicBool = AtomicBool::new(false);
        extern "C" fn fake_notify_audio_effect_ui_appearance_changed(
            appearance: CrasEffectUIAppearance,
        ) {
            CALLED.store(true, Ordering::SeqCst);
            TOGGLE_TYPE.store(appearance.toggle_type.bits(), Ordering::SeqCst);
            EFFECT_MODE_OPTIONS.store(appearance.effect_mode_options.bits(), Ordering::SeqCst);
            SHOW_EFFECT_FALLBACK_MESSAGE
                .store(appearance.show_effect_fallback_message, Ordering::SeqCst);
        }

        s.set_notify_audio_effect_ui_appearance_changed(
            fake_notify_audio_effect_ui_appearance_changed,
        );
        s.update();
        assert!(!CALLED.load(Ordering::SeqCst));
        assert!(!s.output.audio_effects_ready);

        assert!(!s.output.audio_effects_ready);
        assert!(!CALLED.load(Ordering::SeqCst));

        s.set_dlc_installed(NC_AP_DLC, true);
        assert!(!s.output.audio_effects_ready);
        assert!(!CALLED.load(Ordering::SeqCst));

        s.set_dlc_installed(BF_DLC, true);
        assert!(s.output.audio_effects_ready);
        assert!(CALLED.load(Ordering::SeqCst));
        assert_eq!(
            TOGGLE_TYPE.load(Ordering::SeqCst),
            s.output.audio_effect_ui_appearance.toggle_type.bits()
        );
        assert_eq!(
            EFFECT_MODE_OPTIONS.load(Ordering::SeqCst),
            s.output
                .audio_effect_ui_appearance
                .effect_mode_options
                .bits()
        );
        assert_eq!(
            SHOW_EFFECT_FALLBACK_MESSAGE.load(Ordering::SeqCst),
            s.output
                .audio_effect_ui_appearance
                .show_effect_fallback_message
        );
        CALLED.store(false, Ordering::SeqCst);

        s.set_dlc_installed(NC_AP_DLC, false);
        assert!(!s.output.audio_effects_ready);
        assert!(CALLED.load(Ordering::SeqCst));
        assert_eq!(
            TOGGLE_TYPE.load(Ordering::SeqCst),
            s.output.audio_effect_ui_appearance.toggle_type.bits()
        );
        assert_eq!(
            EFFECT_MODE_OPTIONS.load(Ordering::SeqCst),
            s.output
                .audio_effect_ui_appearance
                .effect_mode_options
                .bits()
        );
        assert_eq!(
            SHOW_EFFECT_FALLBACK_MESSAGE.load(Ordering::SeqCst),
            s.output
                .audio_effect_ui_appearance
                .show_effect_fallback_message
        );
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
    fn test_show_effect_fallback_message() {
        let mut s = S2::new();
        assert_eq!(
            s.output
                .audio_effect_ui_appearance
                .show_effect_fallback_message,
            false
        );

        s.set_ap_nc_featured_allowed(true);
        s.set_dlc_installed(NC_AP_DLC, true);
        s.set_ap_nc_segmentation_allowed(true);
        s.set_style_transfer_featured_allowed(true);
        s.set_active_input_node_compatible_nc_providers(CRAS_NC_PROVIDER::all());
        assert_eq!(
            s.output.audio_effect_ui_appearance.toggle_type,
            EFFECT_TYPE::STYLE_TRANSFER
        );
        assert_eq!(
            s.output
                .audio_effect_ui_appearance
                .show_effect_fallback_message,
            false
        );
        s.set_active_input_node_compatible_nc_providers(CRAS_NC_PROVIDER::AP);
        assert_eq!(
            s.output
                .audio_effect_ui_appearance
                .show_effect_fallback_message,
            true
        );
    }

    #[test]
    fn test_system_valid_nc_providers() {
        let mut s = S2::new();
        assert_eq!(s.output.system_valid_nc_providers, CRAS_NC_PROVIDER::NONE);

        s.set_dsp_nc_supported(true);
        assert_eq!(s.output.system_valid_nc_providers, CRAS_NC_PROVIDER::DSP);

        s.set_ap_nc_featured_allowed(true);
        s.set_dlc_installed(NC_AP_DLC, true);
        assert_eq!(
            s.output.system_valid_nc_providers,
            CRAS_NC_PROVIDER::AP | CRAS_NC_PROVIDER::DSP
        );

        s.set_ap_nc_segmentation_allowed(true);
        s.set_style_transfer_featured_allowed(true);
        assert_eq!(
            s.output.system_valid_nc_providers,
            CRAS_NC_PROVIDER::AP | CRAS_NC_PROVIDER::DSP | CRAS_NC_PROVIDER::AST
        );

        s.set_beamforming_config(BeamformingConfig::Supported(BeamformingProperties {
            required_dlcs: HashSet::from([BF_DLC.to_string()]),
            ..Default::default()
        }));
        s.set_dlc_installed(BF_DLC, true);

        s.set_voice_isolation_ui_preferred_effect(EFFECT_TYPE::STYLE_TRANSFER);
        assert_eq!(
            s.output.system_valid_nc_providers,
            CRAS_NC_PROVIDER::AP | CRAS_NC_PROVIDER::DSP | CRAS_NC_PROVIDER::AST
        );
        s.set_voice_isolation_ui_preferred_effect(EFFECT_TYPE::BEAMFORMING);
        assert_eq!(
            s.output.system_valid_nc_providers,
            CRAS_NC_PROVIDER::AP
                | CRAS_NC_PROVIDER::DSP
                | CRAS_NC_PROVIDER::AST
                | CRAS_NC_PROVIDER::BF
        );
        s.set_voice_isolation_ui_preferred_effect(EFFECT_TYPE::NOISE_CANCELLATION);
        assert_eq!(
            s.output.system_valid_nc_providers,
            CRAS_NC_PROVIDER::AP | CRAS_NC_PROVIDER::DSP
        );
    }

    #[test]
    fn test_resolve_nc_provider() {
        let mut s = S2::new();
        s.set_voice_isolation_ui_enabled(true);

        // Set DSP and AP NC supported and allowed.
        s.set_dsp_nc_supported(true);
        s.set_ap_nc_featured_allowed(true);
        s.set_dlc_installed(NC_AP_DLC, true);
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

        // Test for UI preferred effect
        s.set_beamforming_config(BeamformingConfig::Supported(BeamformingProperties {
            required_dlcs: HashSet::from([BF_DLC.to_string()]),
            ..Default::default()
        }));
        s.set_dlc_installed(BF_DLC, true);
        s.set_voice_isolation_ui_preferred_effect(EFFECT_TYPE::STYLE_TRANSFER);
        assert_eq!(
            s.resolve_nc_provider(CRAS_NC_PROVIDER::all(), true),
            &CRAS_NC_PROVIDER::AST
        );
        s.set_voice_isolation_ui_preferred_effect(EFFECT_TYPE::BEAMFORMING);
        assert_eq!(
            s.resolve_nc_provider(CRAS_NC_PROVIDER::all(), true),
            &CRAS_NC_PROVIDER::BF
        );
    }

    #[test]
    fn test_reset_iodev_list_for_voice_isolation() {
        let mut s = S2::new();
        // Set AP NC and AST supported, DLC not installed yet.
        s.set_ap_nc_segmentation_allowed(true);
        s.set_ap_nc_featured_allowed(true);
        s.set_style_transfer_featured_allowed(true);

        static CALLED: AtomicBool = AtomicBool::new(false);
        extern "C" fn fake_reset_iodev_list_for_voice_isolation() {
            CALLED.store(true, Ordering::SeqCst);
        }
        s.set_reset_iodev_list_for_voice_isolation(fake_reset_iodev_list_for_voice_isolation);
        s.update();
        assert!(!CALLED.load(Ordering::SeqCst));

        // Install the DLC should set AST and AP NC allowed.
        s.set_dlc_installed(NC_AP_DLC, true);
        assert!(CALLED.load(Ordering::SeqCst));

        CALLED.store(false, Ordering::SeqCst);
        s.set_voice_isolation_ui_preferred_effect(EFFECT_TYPE::STYLE_TRANSFER);
        // Set BF as supported and allowed.
        s.set_beamforming_config(BeamformingConfig::Supported(BeamformingProperties {
            required_dlcs: HashSet::from([BF_DLC.to_string()]),
            ..Default::default()
        }));
        s.set_dlc_installed(BF_DLC, true);
        // No need to reset iodev list because the preferred effect is AST.
        assert!(!CALLED.load(Ordering::SeqCst));

        s.set_voice_isolation_ui_preferred_effect(EFFECT_TYPE::BEAMFORMING);
        assert!(CALLED.load(Ordering::SeqCst));

        // Test changing "enabled".
        CALLED.store(false, Ordering::SeqCst);
        // Enabled not changed.
        s.set_voice_isolation_ui_enabled(false);
        assert!(!CALLED.load(Ordering::SeqCst));
        // Enabled changed.
        s.set_voice_isolation_ui_enabled(true);
        assert!(CALLED.load(Ordering::SeqCst));
        CALLED.store(false, Ordering::SeqCst);
        s.set_voice_isolation_ui_enabled(false);
        assert!(CALLED.load(Ordering::SeqCst));
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

    #[test]
    fn test_spatial_audio_supported() {
        let mut s = S2::new();
        assert_eq!(s.input.spatial_audio_supported, false);

        s.set_spatial_audio_supported(true);
        assert_eq!(s.input.spatial_audio_supported, true);

        s.set_spatial_audio_supported(false);
        assert_eq!(s.input.spatial_audio_supported, false);
    }
}
