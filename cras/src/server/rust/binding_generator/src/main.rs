// Copyright 2019 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
extern crate cbindgen;

use cbindgen::Builder;

const HEADER_HEAD: &str = "// Copyright";
const HEADER_TAIL: &str = "The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Generated from files in cras/src/server/rust in adhd.";

fn main() {
    Builder::new()
        .with_src("../src/rate_estimator.rs")
        .with_src("../src/rate_estimator_bindings.rs")
        .rename_item("RateEstimator", "rate_estimator")
        .rename_item("timespec", "struct timespec")
        .with_no_includes()
        .with_sys_include("time.h")
        .with_include_guard("RATE_ESTIMATOR_H_")
        .with_language(cbindgen::Language::C)
        .with_header(format!("{} {} {}", HEADER_HEAD, 2019, HEADER_TAIL))
        .generate()
        .expect("Unable to generate bindings")
        .write_to_file("../include/rate_estimator.h");

    Builder::new()
        .with_src("../src/feature_tier.rs")
        .rename_item("CrasFeatureTier", "cras_feature_tier")
        .with_no_includes()
        .with_include_guard("CRAS_FEATURE_TIER_H_")
        .with_language(cbindgen::Language::C)
        .with_header(format!("{} {} {}", HEADER_HEAD, 2022, HEADER_TAIL))
        .generate()
        .expect("Unable to generate bindings")
        .write_to_file("../include/cras_feature_tier.h");

    Builder::new()
        .with_src("../cras_dlc/src/lib.rs")
        .with_no_includes()
        .with_include_guard("CRAS_DLC_H_")
        .with_language(cbindgen::Language::C)
        .with_header(format!("{} {} {}", HEADER_HEAD, 2022, HEADER_TAIL))
        .generate()
        .expect("Unable to generate bindings")
        .write_to_file("../include/cras_dlc.h");
}
