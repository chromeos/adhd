// Copyright 2019 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
use bindgen::builder;

use std::fs::File;
use std::io::Write;
use std::path::Path;

fn gen(header_dir: &Path) -> String {
    let name = "cras_gen";
    let bindings = builder()
        .header(header_dir.join("cras_messages.h").to_str().unwrap())
        .header(header_dir.join("cras_types.h").to_str().unwrap())
        .header(header_dir.join("cras_audio_format.h").to_str().unwrap())
        .header(header_dir.join("cras_shm.h").to_str().unwrap())
        .whitelist_type("cras_.*")
        .whitelist_var("cras_.*")
        .whitelist_type("CRAS_.*")
        .whitelist_var("CRAS_.*")
        .whitelist_type("audio_message")
        .whitelist_var("MAX_DEBUG_.*")
        .rustified_enum("CRAS_.*")
        .rustified_enum("_snd_pcm_.*")
        .bitfield_enum("CRAS_STREAM_EFFECT")
        .generate()
        .unwrap_or_else(|_| panic!("Unable to generate {} code", name));

    bindings.to_string()
}

fn write_output(output_path: &Path, output: String) -> std::io::Result<()> {
    let mut output_file = File::create(output_path)?;
    output_file.write_all(output.as_bytes())?;
    Ok(())
}

fn main() {
    let src_file = Path::new("./src/gen.rs");
    let src_header_dir = Path::new("../../src/common");
    if src_file.exists() {
        return;
    }

    let generated_code = gen(&src_header_dir);
    write_output(src_file, generated_code).expect("failed to write generated code");
}
