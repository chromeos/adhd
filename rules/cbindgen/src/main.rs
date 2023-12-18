// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use cbindgen::Builder;
use cbindgen::Config;
use clap::Parser;

fn header(copyright_year: u32) -> String {
    format!(
        "// Copyright {copyright_year} The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Generated from files in cras/src/server/rust in adhd.
// clang-format off

#ifdef __cplusplus
extern \"C\" {{
#endif"
    )
}

const TRAILER: &str = "#ifdef __cplusplus
}
#endif
";

fn include_guard(path: &str) -> String {
    format!(
        "{}_",
        path.to_uppercase().replace(".", "_").replace("/", "_")
    )
}

#[derive(Parser)]
struct Command {
    #[clap(long)]
    output: String,

    #[clap(long)]
    assume_output: Option<String>,

    #[clap(long)]
    with_src: Vec<String>,

    #[clap(long)]
    with_include: Vec<String>,

    #[clap(long)]
    with_sys_include: Vec<String>,

    #[clap(long, default_value = "2023")]
    copyright_year: u32,
}

fn main() {
    let c = Command::parse();

    let config = Config {
        usize_is_size_t: true,
        ..Default::default()
    };

    let b = Builder::new()
        .with_config(config)
        .with_language(cbindgen::Language::C)
        .with_style(cbindgen::Style::Tag)
        .with_header(header(c.copyright_year))
        .with_trailer(TRAILER)
        .with_include_guard(include_guard(
            c.assume_output.as_deref().unwrap_or(&c.output),
        ));

    let b = b
        .rename_item("timespec", "struct timespec")
        .rename_item("plugin_processor", "struct plugin_processor")
        .rename_item("RateEstimator", "rate_estimator")
        .rename_item("CrasFeatureTier", "cras_feature_tier")
        .rename_item("CrasFRASignal", "CRAS_FRA_SIGNAL")
        .rename_item("KeyValuePair", "cras_fra_kv_t");

    let mut b = b;
    for src in c.with_src {
        b = b.with_src(src);
    }
    for include in c.with_include {
        b = b.with_include(include);
    }
    for sys_include in c.with_sys_include {
        b = b.with_sys_include(sys_include);
    }

    b.generate()
        .unwrap_or_else(|e| panic!("cannot generate {}: {e}", c.output))
        .write_to_file(c.output);
}
