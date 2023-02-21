// Copyright 2019 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
extern crate cbindgen;

use std::path::Path;

use cbindgen::Builder;

const HEADER_HEAD: &str = "// Copyright";
const HEADER_TAIL: &str = "The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Generated from files in cras/src/server/rust in adhd.
// clang-format off

#ifdef __cplusplus
extern \"C\" {
#endif";

const TRAILER: &str = "#ifdef __cplusplus
}
#endif
";

fn builder(copyright_year: u32) -> Builder {
    Builder::new()
        .rename_item("timespec", "struct timespec")
        .rename_item("plugin_processor", "struct plugin_processor")
        .with_language(cbindgen::Language::C)
        .with_style(cbindgen::Style::Tag)
        .with_header(format!(
            "{} {} {}",
            HEADER_HEAD, copyright_year, HEADER_TAIL
        ))
        .with_trailer(TRAILER)
}

fn generate(b: Builder, filename: &str) {
    b.with_include_guard(format!(
        "CRAS_SRC_SERVER_RUST_INCLUDE_{}_",
        filename.to_uppercase().replace('.', "_")
    ))
    .generate()
    .unwrap_or_else(|_| panic!("cannot generate {}", filename))
    .write_to_file(Path::new("../include").join(filename));
}

fn main() {
    generate(
        builder(2019)
            .with_src("../src/rate_estimator.rs")
            .with_src("../src/rate_estimator_bindings.rs")
            .rename_item("RateEstimator", "rate_estimator")
            .with_sys_include("time.h"),
        "rate_estimator.h",
    );

    generate(
        builder(2022)
            .with_src("../src/feature_tier.rs")
            .rename_item("CrasFeatureTier", "cras_feature_tier"),
        "cras_feature_tier.h",
    );

    generate(
        builder(2022).with_src("../cras_dlc/src/lib.rs"),
        "cras_dlc.h",
    );

    generate(
        builder(2023).with_src("../src/logging.rs"),
        "cras_rust_logging.h",
    );

    generate(
        builder(2023).with_src("../src/cras_processor.rs"),
        "cras_processor.h",
    );
}
