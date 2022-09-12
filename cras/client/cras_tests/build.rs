// Copyright 2021 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Generates the Rust D-Bus bindings for cras_tests.

use std::path::Path;

use chromeos_dbus_bindings::{self, generate_module, BindingsType};

const SOURCE_DIR: &str = ".";

// (<module name>, <relative path to source xml>, <bindings type>)
const BINDINGS_TO_GENERATE: &[(&str, &str, BindingsType)] = &[(
    "org_chromium_cras_control",
    "../../dbus_bindings/org.chromium.cras.Control.xml",
    BindingsType::Client(None),
)];

fn main() {
    if let Err(e) = generate_module(Path::new(SOURCE_DIR), BINDINGS_TO_GENERATE) {
        panic!("{}", e);
    }
}
