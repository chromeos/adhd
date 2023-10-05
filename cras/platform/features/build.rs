// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use std::env;
use std::fmt::Write;
use std::path::Path;
use std::path::PathBuf;

use regex::Regex;

extern crate bindgen;

fn main() {
    let out_path = PathBuf::from(env::var("OUT_DIR").unwrap());

    println!("cargo:rerun-if-changed=features.inc");
    println!("cargo:rerun-if-changed=features.h");
    println!("cargo:rerun-if-changed=features_impl.h");

    gen_feature_decls(&out_path);
    gen_bindings(&out_path);
}

fn gen_feature_decls(out_path: &Path) {
    let features = std::fs::read_to_string("features.inc").expect("cannot read features");

    let re = Regex::new(r"(?m)^DEFINE_FEATURE\((\w+), (true|false)\)$").unwrap();

    let mut content = format!(
        r"pub struct FeatureDecl {{
    pub name: &'static str,
    pub default_enabled: bool,
}}

pub const FEATURES: [FeatureDecl; {}] = [
",
        re.find_iter(&features).count()
    );

    for m in re.captures_iter(&features) {
        let name = m.get(1).unwrap().as_str();
        let default_enabled = m.get(2).unwrap().as_str() == "true";
        writeln!(
            content,
            "    FeatureDecl {{ name: {name:?}, default_enabled: {default_enabled}}},"
        )
        .unwrap();
    }

    writeln!(content, "];").unwrap();

    std::fs::write(out_path.join("feature_decls.rs"), content).unwrap();
}

fn gen_bindings(out_path: &Path) {
    bindgen::Builder::default()
        .clang_arg("-I.")
        .header("features_impl.h")
        .allowlist_type("cras_feature_id")
        .allowlist_type("cras_features_notify_changed")
        .blocklist_function(".*")
        .generate()
        .expect("bindgen generate")
        .write_to_file(out_path.join("cras_features_bindings.rs"))
        .expect("bindgen write_to_file");
}
