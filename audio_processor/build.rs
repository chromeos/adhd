// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use std::env;
use std::ffi::OsStr;
use std::path::PathBuf;
use std::process::Command;

extern crate bindgen;

fn pkg_config<I, S>(args: I) -> Vec<String>
where
    I: IntoIterator<Item = S>,
    S: AsRef<OsStr>,
{
    let output =
        Command::new(env::var("PKG_CONFIG").unwrap_or_else(|_| String::from("pkg-config")))
            .args(args)
            .output()
            .expect("failed to run pkg-config");
    if !output.status.success() {
        panic!(
            "pkg-config exited with status {}; stderr:\n{}",
            output.status,
            String::from_utf8_lossy(&output.stderr)
        );
    }
    let stdout = String::from_utf8(output.stdout).expect("pkg-config returned non-UTF-8");
    stdout.split_ascii_whitespace().map(String::from).collect()
}

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

    let speex_cflags = pkg_config(["--cflags", "speexdsp"]);
    eprintln!("speex cflags: {:?}", speex_cflags);
    let mut builder = bindgen::Builder::default();
    if let Ok(sysroot) = env::var("SYSROOT") {
        // Set sysroot so includes in /build/${BOARD} can be found.
        builder = builder.clang_arg("--sysroot").clang_arg(sysroot);
    }
    builder
        .clang_args(speex_cflags)
        .header("speex_bindgen.h")
        .generate()
        .expect("Cannot generate speex bindings")
        .write_to_file(out_path.join("speex_sys.rs"))
        .expect("Cannot write speex bindings");
    for link_arg in pkg_config(["--libs", "speexdsp"]) {
        if let Some(lib) = link_arg.strip_prefix("-l") {
            println!("cargo:rustc-link-lib={}", lib);
        } else {
            println!("cargo:rustc-link-arg={}", link_arg);
        }
    }

    println!("cargo:rerun-if-changed=proto/");
    protobuf_codegen::Codegen::new()
        .pure()
        .include("proto")
        .input("proto/cdcfg.proto")
        .cargo_out_dir("proto")
        .run_from_script();
}
