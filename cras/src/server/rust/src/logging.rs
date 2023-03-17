// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use anyhow::anyhow;
use log::LevelFilter;
use syslog::{BasicLogger, Facility, Formatter3164};

fn init_logging() -> anyhow::Result<()> {
    let formatter = Formatter3164 {
        facility: Facility::LOG_USER,
        hostname: None,
        process: "cras_server".into(),
        pid: 0,
    };

    let logger = syslog::unix(formatter).map_err(
        // We coerce syslog::Error to a string because it is not Sync,
        // which is required by anyhow::Error.
        |e| anyhow!("cannot connect to syslog: {:?}", e),
    )?;
    log::set_boxed_logger(Box::new(BasicLogger::new(logger)))?;

    // TODO: Respect log level set from the command line.
    log::set_max_level(LevelFilter::Info);

    Ok(())
}

pub mod bindings {
    #[no_mangle]
    /// Initialize logging for cras_rust.
    /// Recommended to be called before all other cras_rust functions.
    pub extern "C" fn cras_rust_init_logging() -> libc::c_int {
        match super::init_logging() {
            Ok(_) => 0,
            Err(e) => {
                eprintln!("error in init_logging: {:?}", e);
                -libc::EAGAIN
            }
        }
    }
}
