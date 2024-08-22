// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use std::collections::HashSet;

use cras_common::types_internal::CrasDlcId;
use serde::Serialize;

pub mod global;

#[derive(Serialize)]
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
    style_transfer_enabled: bool,
    // cros_config /audio/main cras-config-dir.
    cras_config_dir: String,
}

#[derive(Serialize)]
struct Output {
    ap_nc_allowed: bool,
    style_transfer_supported: bool,
    style_transfer_allowed: bool,
    style_transfer_enabled: bool,
    beamforming_supported: bool,
    beamforming_allowed: bool,
}

fn resolve(input: &Input) -> Output {
    // TODO(b/339785214): Decide this based on config file content.
    let beamforming_supported = input.cras_config_dir.ends_with(".3mic");
    let dlc_nc_ap_installed = input
        .dlc_installed
        .contains(CrasDlcId::CrasDlcNcAp.as_str());
    Output {
        ap_nc_allowed: (input.ap_nc_featured_allowed
            || input.ap_nc_segmentation_allowed
            || input.ap_nc_feature_tier_allowed)
            && dlc_nc_ap_installed,
        style_transfer_supported: input.ap_nc_segmentation_allowed && !beamforming_supported,
        style_transfer_allowed: input.style_transfer_featured_allowed && dlc_nc_ap_installed,
        style_transfer_enabled: input.style_transfer_enabled,
        beamforming_supported,
        beamforming_allowed: input
            .dlc_installed
            .contains(CrasDlcId::CrasDlcIntelligoBeamforming.as_str()),
    }
}

#[derive(Serialize)]
struct S2 {
    input: Input,
    output: Output,
}

impl S2 {
    fn new() -> Self {
        let input = Input {
            ap_nc_featured_allowed: false,
            ap_nc_segmentation_allowed: false,
            ap_nc_feature_tier_allowed: false,
            dlc_manager_ready: false,
            dlc_installed: HashSet::new(),
            dlc_manager_done: false,
            style_transfer_featured_allowed: false,
            style_transfer_enabled: false,
            cras_config_dir: String::new(),
        };
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

    fn set_style_transfer_enabled(&mut self, enabled: bool) {
        self.input.style_transfer_enabled = enabled;
        self.update();
    }

    fn set_cras_config_dir(&mut self, cras_config_dir: &str) {
        self.input.cras_config_dir = cras_config_dir.into();
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
    use std::collections::HashSet;

    use cras_common::types_internal::CrasDlcId;

    use crate::S2;

    #[test]
    fn test_ap_nc() {
        let mut s = S2::new();
        assert_eq!(s.output.ap_nc_allowed, false);

        s.set_ap_nc_featured_allowed(true);
        assert_eq!(s.output.ap_nc_allowed, false);

        s.set_ap_nc_featured_allowed(false);
        s.set_ap_nc_segmentation_allowed(true);
        assert_eq!(s.output.ap_nc_allowed, false);

        s.set_ap_nc_segmentation_allowed(false);
        s.set_ap_nc_feature_tier_allowed(true);
        assert_eq!(s.output.ap_nc_allowed, false);

        s.set_dlc_installed(CrasDlcId::CrasDlcNcAp);

        s.set_ap_nc_featured_allowed(true);
        assert_eq!(s.output.ap_nc_allowed, true);

        s.set_ap_nc_featured_allowed(false);
        s.set_ap_nc_segmentation_allowed(true);
        assert_eq!(s.output.ap_nc_allowed, true);

        s.set_ap_nc_segmentation_allowed(false);
        s.set_ap_nc_feature_tier_allowed(true);
        assert_eq!(s.output.ap_nc_allowed, true);
    }

    #[test]
    fn test_style_transfer_supported() {
        let mut s = S2::new();
        assert_eq!(s.output.style_transfer_supported, false);

        s.set_ap_nc_segmentation_allowed(true);
        assert_eq!(s.output.style_transfer_supported, true);

        s.set_cras_config_dir("omniknight.3mic");
        assert_eq!(s.output.style_transfer_supported, false);
    }

    #[test]
    fn test_style_transfer_allowed() {
        let mut s = S2::new();
        assert_eq!(s.output.style_transfer_allowed, false);

        s.set_style_transfer_featured_allowed(true);
        assert_eq!(s.output.style_transfer_allowed, false);

        s.set_dlc_installed(CrasDlcId::CrasDlcNcAp);
        assert_eq!(s.output.style_transfer_allowed, true);

        s.set_style_transfer_featured_allowed(false);
        assert_eq!(s.output.style_transfer_allowed, false);
    }

    #[test]
    fn test_style_transfer_enabled() {
        let mut s = S2::new();
        assert_eq!(s.output.style_transfer_enabled, false);

        s.set_style_transfer_enabled(true);
        assert_eq!(s.output.style_transfer_enabled, true);

        s.set_style_transfer_enabled(false);
        assert_eq!(s.output.style_transfer_enabled, false);
    }

    #[test]
    fn test_beamforming() {
        let mut s = S2::new();
        s.set_ap_nc_segmentation_allowed(true);
        s.set_style_transfer_featured_allowed(true);
        assert!(!s.output.beamforming_supported);
        assert!(s.output.style_transfer_supported);

        s.set_cras_config_dir("omniknight.3mic");
        assert!(s.output.beamforming_supported);
        assert!(!s.output.style_transfer_supported);

        assert!(!s.output.beamforming_allowed);
        s.set_dlc_installed(CrasDlcId::CrasDlcIntelligoBeamforming);
        assert!(s.output.beamforming_allowed);

        s.set_cras_config_dir("omniknight");
        assert!(!s.output.beamforming_supported);
        assert!(s.output.style_transfer_supported);
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
}
