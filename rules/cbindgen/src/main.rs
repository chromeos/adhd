// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use cbindgen::Builder;
use cbindgen::Config;
use cbindgen::MacroExpansionConfig;
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
    #[arg(long)]
    output: String,

    #[arg(long)]
    assume_output: Option<String>,

    #[arg(long)]
    with_src: Vec<String>,

    #[arg(long)]
    with_include: Vec<String>,

    #[arg(long)]
    with_sys_include: Vec<String>,

    #[arg(long, default_value = "2023")]
    copyright_year: u32,

    #[arg(
        long,
        help = "Add the `enum` keyword to generated enums. Useful if the enums are not automatically prefixed with `enum`."
    )]
    add_keyword_enum: bool,

    #[arg(
        long,
        help = "Add the `struct` keyword to generated structs. Useful if the structs are not automatically prefixed with `struct`."
    )]
    add_keyword_struct: bool,
}

fn main() {
    // CRAS_CBINDGEN_LOG=warn to turn on warning.
    // Or pass --//rules/cbindgen:log_level=warn to bazel.
    env_logger::init_from_env(env_logger::Env::new().filter("CRAS_CBINDGEN_LOG"));

    let c = Command::parse();

    let config = Config {
        usize_is_size_t: true,
        macro_expansion: MacroExpansionConfig { bitflags: true },
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
        .rename_item("RateEstimatorHandle", "rate_estimator")
        .rename_item("CrasFeatureTier", "cras_feature_tier")
        .rename_item("CrasFRASignal", "CRAS_FRA_SIGNAL")
        .rename_item("KeyValuePair", "cras_fra_kv_t")
        .rename_item("DCBlock", "dcblock")
        .rename_item("Biquad", "biquad")
        .rename_item("BiquadType", "biquad_type")
        .rename_item("EQ", "eq")
        .rename_item("Crossover", "crossover")
        .rename_item("EQ2", "eq2")
        .rename_item("Crossover2", "crossover2")
        .rename_item("LR42", "lr42")
        .rename_item("DrcKernel", "drc_kernel")
        .rename_item("DrcKernelParam", "drc_kernel_param")
        .rename_item("DRC", "drc")
        .rename_item("DRCComponent", "drc_component")
        .rename_item("DRC_PARAM", "drc_param")
        .include_item("DRC_PARAM")
        .include_item("CrasDlcId")
        .include_item("EFFECT_TYPE")
        .include_item("CrasProcessorEffect")
        .include_item("CrasEffectUIAppearance");

    let mut b = b;

    if c.add_keyword_enum {
        b = b
            .rename_item("CrasDlcId", "enum CrasDlcId")
            .rename_item("CrasProcessorEffect", "enum CrasProcessorEffect");
    }

    if c.add_keyword_struct {
        b = b.rename_item("CrasEffectUIAppearance", "struct CrasEffectUIAppearance");
    }

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
