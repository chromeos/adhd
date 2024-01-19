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
    let config = cbindgen::Config {
        usize_is_size_t: true,
        ..Default::default()
    };
    Builder::new()
        .with_config(config)
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
    .unwrap_or_else(|e| panic!("cannot generate {filename}: {e}"))
    .write_to_file(Path::new("cras/src/server/rust/include").join(filename));
}

fn main() {
    std::env::set_current_dir("../../../../..").unwrap();

    generate(
        builder(2023)
            .with_src("cras/src/server/rust/src/cras_processor.rs")
            .with_include("audio_processor/c/plugin_processor.h"),
        "cras_processor.h",
    );
}
