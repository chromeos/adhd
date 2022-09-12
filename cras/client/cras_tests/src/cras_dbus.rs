// Copyright 2021 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
use std::time::Duration;

use dbus::blocking::Connection;
use libcras::CrasScreenRotation;
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
    GetOutputVolume,
    SetOutputVolume(i32),
    /// (num_channels, coefficients)
    SetGlobalOutputChannelRemix(u32, Vec<f64>),
    SetDisplayRotation(u64, CrasScreenRotation),
    SetFlossEnabled(bool),
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
            Self::GetOutputVolume => {
                println!("{}", proxy.get_volume_state().map_err(Error::DBusCall)?.0);
            }
            Self::SetOutputVolume(volume) => {
                proxy.set_output_volume(volume).map_err(Error::DBusCall)?;
            }
            Self::SetGlobalOutputChannelRemix(num_channels, coefficients) => {
                proxy
                    .set_global_output_channel_remix(num_channels as i32, coefficients)
                    .map_err(Error::DBusCall)?;
            }
            Self::SetDisplayRotation(node_id, rotation) => {
                proxy
                    .set_display_rotation(node_id, rotation as u32)
                    .map_err(Error::DBusCall)?;
            }
            Self::SetFlossEnabled(enabled) => {
                proxy.set_floss_enabled(enabled).map_err(Error::DBusCall)?;
            }
        }
        Ok(())
    }
}
