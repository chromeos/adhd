// Copyright 2019 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
use std::env;
use std::fs::remove_file;
use std::path::Path;

use bindgen::builder;

fn main() {
    let gen_file = Path::new("./src/gen.rs");
    if gen_file.exists() {
        if env::var("CROS_RUST") == Ok(String::from("1")) {
            return;
        }
        remove_file(gen_file).expect("Failed to remove generated file.");
    }
    let header_dir = Path::new("../../include");
    let header_path = header_dir.join("cras_bindgen.h");
    println!("cargo:rerun-if-changed={}", header_path.display());
    println!("cargo:rerun-if-changed=src/gen.rs");
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
