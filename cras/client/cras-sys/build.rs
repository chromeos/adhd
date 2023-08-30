// Copyright 2019 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
use std::env;
use std::path::Path;
use std::path::PathBuf;

use bindgen::builder;

fn main() {
    let gen_file = match env::var("CROS_RUST").as_deref() {
        // Building for ChromiumOS.
        Ok("1") => {
            println!("cargo:rustc-env=GEN_FILE=gen.rs"); // Relative to src/.

            let path = PathBuf::from("src/gen.rs");
            if !path.exists() {
                // If gen.rs does not exist, it means we are building the
                // cras-client package. Generate to src/ to allow:
                // 1. src/gen.rs to be copied to cros_rust_registry
                // 2. Skip generation when built as a dependency of another crate.
                path
            } else {
                // If gen.rs already exists, it means cras-sys is located in
                // cros_rust_registry, and is being built as part of another
                // crate's dependency.
                // At this time the sources required to generate gen.rs
                // are not accessible. Skip the generation.
                return;
            }
        }
        // Building out of ChromiumOS.
        _ => {
            // The `header_dir` is always available,
            // so use the standard OUT_DIR environment variable.
            let path = Path::new(
                &env::var("OUT_DIR").expect("missing cargo OUT_DIR environment variable"),
            )
            .join("gen.rs");
            println!("cargo:rustc-env=GEN_FILE={}", path.display());
            path
        }
    };
    let header_dir = Path::new("../../include");
    let header_path = header_dir.join("cras_bindgen.h");
    println!("cargo:rerun-if-changed={}", header_path.display());
    let bindings = builder()
        .header(header_path.to_str().unwrap())
        .allowlist_type("cras_.*")
        .allowlist_var("cras_.*")
        .allowlist_type("CRAS_.*")
        .allowlist_var("CRAS_.*")
        .allowlist_type("audio_message")
        .allowlist_var("MAX_DEBUG_.*")
        .rustified_enum("CRAS_.*")
        .rustified_enum("_snd_pcm_.*")
        .bitfield_enum("CRAS_STREAM_EFFECT")
        .generate()
        .expect("Failed to generate bindings.");
    bindings
        .write_to_file(gen_file.to_str().unwrap())
        .expect("Failed to write bindings to file");
}
