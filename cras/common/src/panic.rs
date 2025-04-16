// Copyright 2025 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use cfg_if::cfg_if;

#[no_mangle]
/// Install a panic hook to allow the panic message to be included in crash reports.
pub extern "C" fn cras_rust_register_panic_hook() {
    cfg_if! {
        if #[cfg(feature = "chromiumos")] {
            libchromeos::panic_handler::install_memfd_handler();
        } else {
            log::info!(r#"cras_rust_register_panic_hook() doing nothing without feature = "chromiumos""#);
        }
    }

    const CRAS_RUST_PANIC_FOR_TESTING: &str = "CRAS_RUST_PANIC_FOR_TESTING";
    if std::env::var(CRAS_RUST_PANIC_FOR_TESTING).is_ok() {
        panic!("panicing due to {CRAS_RUST_PANIC_FOR_TESTING}")
    }
}
