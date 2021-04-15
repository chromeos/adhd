// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
use std::time::Duration;

use dbus::blocking::Connection;
use thiserror::Error as ThisError;

const CRAS_DBUS_DESTINATION: &str = "org.chromium.cras";
const CRAS_DBUS_OBJECT_PATH: &str = "/org/chromium/cras";
const DEFAULT_DBUS_TIMEOUT: Duration = Duration::from_secs(25);

// Errors for dbus operations.
#[derive(ThisError, Debug)]
pub enum Error {
    #[error("failed to get D-Bus connection: {0:}")]
    NewDBusConnection(dbus::Error),
    #[error("failed to call D-Bus method: {0:}")]
    DBusCall(dbus::Error),
}

type Result<T> = std::result::Result<T, Error>;

// DBus operation enumeration for org.chromium.cras.Control interface.
#[derive(Debug, PartialEq)]
pub enum DBusControlOp {
    GetNumberOfActiveStreams,
    GetDefaultOutputBufferSize,
    Introspect,
}

impl DBusControlOp {
    // Consumes operation and makes DBus call.
    pub fn run(self) -> Result<()> {
        // use generated interface here to get proxy implementation.
        use cras_tests::client::{OrgChromiumCrasControl, OrgFreedesktopDBusIntrospectable};

        let conn = Connection::new_system().map_err(Error::NewDBusConnection)?;
        let proxy = conn.with_proxy(
            CRAS_DBUS_DESTINATION,
            CRAS_DBUS_OBJECT_PATH,
            DEFAULT_DBUS_TIMEOUT,
        );

        match self {
            Self::GetNumberOfActiveStreams => {
                println!(
                    "{}",
                    proxy
                        .get_number_of_active_streams()
                        .map_err(Error::DBusCall)?
                );
            }
            Self::GetDefaultOutputBufferSize => {
                println!(
                    "{}",
                    proxy
                        .get_default_output_buffer_size()
                        .map_err(Error::DBusCall)?
                );
            }
            Self::Introspect => {
                println!("{}", proxy.introspect().map_err(Error::DBusCall)?);
            }
        }
        Ok(())
    }
}
