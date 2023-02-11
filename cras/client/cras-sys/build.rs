// Copyright 2019 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
use std::env;
use std::fs::remove_file;
use std::path::Path;
use std::process::Command;

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
    let status = Command::new("bindgen")
        .arg(header_path.to_str().unwrap())
        .args(["--allowlist-type", "cras_.*"])
        .args(["--allowlist-var", "cras_.*"])
        .args(["--allowlist-type", "CRAS_.*"])
        .args(["--allowlist-var", "CRAS_.*"])
        .args(["--allowlist-type", "audio_message"])
        .args(["--allowlist-var", "MAX_DEBUG_.*"])
        .args(["--rustified-enum", "CRAS_.*"])
        .args(["--rustified-enum", "_snd_pcm_.*"])
        .args(["--bitfield-enum", "CRAS_STREAM_EFFECT"])
        .args(["--output", gen_file.to_str().unwrap()])
        .status()
        .expect("Failed in bindgen command.");
    assert!(status.success(), "Got error from bindgen command");
}
