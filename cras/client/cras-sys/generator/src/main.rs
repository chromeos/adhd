// Copyright 2019 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
extern crate bindgen;

use bindgen::builder;

fn gen() {
    let name = "cras_gen";
    let bindings = builder()
        .header("c_headers/cras_messages.h")
        .header("c_headers/cras_types.h")
        .header("c_headers/cras_audio_format.h")
        .whitelist_type("cras_.*")
        .whitelist_var("cras_.*")
        .whitelist_type("CRAS_.*")
        .whitelist_var("CRAS_.*")
        .whitelist_type("audio_message")
        .rustified_enum("CRAS_.*")
        .rustified_enum("_snd_pcm_.*")
        .generate()
        .expect(format!("Unable to generate {} code", name).as_str());
    bindings
        .write_to_file("lib_gen.rs")
        .expect("Unable to generate lib.rs file");
}

fn main() {
    gen();
}
