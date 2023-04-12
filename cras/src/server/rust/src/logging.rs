// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use std::convert::TryInto;
use std::io::Write;

use anyhow::anyhow;
use log::Level;
use log::LevelFilter;
use log::Log;
use nix::unistd::getpid;
use syslog::BasicLogger;
use syslog::Facility;
use syslog::Formatter3164;
use syslog::LogFormat;
use syslog::Severity;

/// A struct that represents a logger that logs to stderr.
struct StderrLogger {
    formatter: Formatter3164,
}

impl Log for StderrLogger {
    fn enabled(&self, _metadata: &log::Metadata) -> bool {
        true
    }

    fn log(&self, record: &log::Record) {
        let mut stderr = std::io::stderr().lock();
        let severity = match record.level() {
            Level::Error => Severity::LOG_ERR,
            Level::Warn => Severity::LOG_WARNING,
            Level::Info => Severity::LOG_INFO,
            Level::Debug => Severity::LOG_DEBUG,
            Level::Trace => Severity::LOG_DEBUG,
        };
        let message = format!("{}\n", record.args());
        let _ = self.formatter.format(&mut stderr, severity, message);
    }

    fn flush(&self) {
        let _ = std::io::stderr().lock().flush();
    }
}

/// A struct that represents a logger that logs to two other loggers.
struct DualLogger<T: Log, U: Log>(T, U);

impl<T: Log, U: Log> Log for DualLogger<T, U> {
    fn enabled(&self, _metadata: &log::Metadata) -> bool {
        true
    }

    fn log(&self, record: &log::Record) {
        self.0.log(record);
        self.1.log(record);
    }

    fn flush(&self) {
        self.0.flush();
        self.1.flush();
    }
}

fn init_logging() -> anyhow::Result<()> {
    let formatter = Formatter3164 {
        facility: Facility::LOG_USER,
        hostname: None,
        process: "cras_server".into(),
        pid: getpid().as_raw().try_into().unwrap_or(0),
    };

    let stderr_logger = StderrLogger {
        formatter: formatter.clone(),
    };
    let sys_logger = syslog::unix(formatter).map_err(
        // We coerce syslog::Error to a string because it is not Sync,
        // which is required by anyhow::Error.
        |e| anyhow!("cannot connect to syslog: {:?}", e),
    )?;
    let logger = DualLogger(stderr_logger, BasicLogger::new(sys_logger));
    log::set_boxed_logger(Box::new(logger))?;

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
