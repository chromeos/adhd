// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use std::{
    env,
    path::PathBuf,
    process::{Command, Stdio},
};

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

    if cfg!(feature = "host_cc") {
        let mut cc = cc::Build::new();
        cc.flag("-fPIC")
            .file("c/bad_plugin.c")
            .file("c/negate_plugin.c")
            .include("c");

        cc.compile("test_plugins");
        let status = Command::new(cc.get_compiler().path())
            .arg("-o")
            .arg(out_path.join("libtest_plugins.so").into_os_string())
            .arg("-shared")
            .arg("-Wl,--whole-archive")
            .arg(out_path.join("libtest_plugins.a").into_os_string())
            .arg("-Wl,--no-whole-archive")
            .stdin(Stdio::null())
            .status()
            .expect("failed to build shared library");
        if !status.success() {
            panic!("libtest_plugins.so build failed with {}", status);
        }
    }
}
