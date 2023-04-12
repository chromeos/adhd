// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use std::env;
use std::path::PathBuf;

extern crate bindgen;

fn main() {
    let out_path = PathBuf::from(env::var("OUT_DIR").unwrap());

    println!("cargo:rerun-if-changed=c/");

    bindgen::Builder::default()
        .header("c/plugin_processor.h")
        .header("c/bad_plugin.h")
        .header("c/negate_plugin.h")
        .parse_callbacks(Box::new(bindgen::CargoCallbacks))
        .newtype_enum("status")
        .size_t_is_usize(true)
        .generate()
        .expect("Cannot generate bindings")
        .write_to_file(out_path.join("plugin_processor_binding.rs"))
        .expect("Cannot write bindings");
}
