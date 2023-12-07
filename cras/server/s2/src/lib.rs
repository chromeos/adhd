// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use serde::Serialize;

pub mod global;

#[derive(Serialize)]
struct Input {
    ap_nc_featured_allowed: bool,
    ap_nc_segmentation_allowed: bool,
}

#[derive(Serialize)]
struct Output {
    ap_nc_allowed: bool,
}

fn resolve(input: &Input) -> Output {
    Output {
        ap_nc_allowed: input.ap_nc_featured_allowed || input.ap_nc_segmentation_allowed,
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
}
