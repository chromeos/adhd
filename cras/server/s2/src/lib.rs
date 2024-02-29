// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use serde::Serialize;

pub mod global;

#[derive(Serialize)]
struct Input {
    ap_nc_featured_allowed: bool,
    ap_nc_segmentation_allowed: bool,
    /// Tells whether the DLC manager is ready.
    /// Used by tests to avoid races.
    dlc_manager_ready: bool,
    style_transfer_featured_allowed: bool,
    style_transfer_enabled: bool,
}

#[derive(Serialize)]
struct Output {
    ap_nc_allowed: bool,
    style_transfer_enabled: bool,
}

fn resolve(input: &Input) -> Output {
    Output {
        ap_nc_allowed: input.ap_nc_featured_allowed || input.ap_nc_segmentation_allowed,
        // It's 'or' here because before the toggle of StyleTransfer is landed, users
        // should be able to control the feature only by the feature flag and there
        // would be only tests writing its system state currently.
        // TODO(b/327530996): handle tests: enabled without featured allowed.
        style_transfer_enabled: input.style_transfer_featured_allowed
            || input.style_transfer_enabled,
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
            dlc_manager_ready: false,
            style_transfer_featured_allowed: false,
            style_transfer_enabled: false,
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

    fn set_dlc_manager_ready(&mut self) {
        self.input.dlc_manager_ready = true;
        self.update();
    }

    fn set_style_transfer_featured_allowed(&mut self, allowed: bool) {
        self.input.style_transfer_featured_allowed = allowed;
        self.update();
    }

    fn set_style_transfer_enabled(&mut self, enabled: bool) {
        self.input.style_transfer_enabled = enabled;
        self.update();
    }

    fn update(&mut self) {
        self.output = resolve(&self.input);
    }
}

#[cfg(test)]
mod tests {
    use crate::S2;

    #[test]
    fn test_ap_nc() {
        let mut s = S2::new();
        assert_eq!(s.output.ap_nc_allowed, false);

        s.set_ap_nc_featured_allowed(true);
        assert_eq!(s.output.ap_nc_allowed, true);

        s.set_ap_nc_featured_allowed(false);
        s.set_ap_nc_segmentation_allowed(true);
        assert_eq!(s.output.ap_nc_allowed, true);
    }

    #[test]
    fn test_style_transfer_enabled() {
        let mut s = S2::new();
        assert_eq!(s.output.style_transfer_enabled, false);

        s.set_style_transfer_enabled(true);
        assert_eq!(s.output.style_transfer_enabled, true);

        s.set_style_transfer_featured_allowed(true);
        assert_eq!(s.output.style_transfer_enabled, true);

        s.set_style_transfer_enabled(false);
        assert_eq!(s.output.style_transfer_enabled, true);

        s.set_style_transfer_featured_allowed(false);
        assert_eq!(s.output.style_transfer_enabled, false);
    }
}
