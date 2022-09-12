// Copyright 2021 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
use thiserror::Error as ThisError;

use crate::cras_dbus::{self, DBusControlOp};

// Errors for get command.
#[derive(ThisError, Debug)]
pub enum Error {
    #[error("failed in cras_dbus: {0:}")]
    CrasDBus(cras_dbus::Error),
}

type Result<T> = std::result::Result<T, Error>;

#[derive(PartialEq, Debug, clap::Subcommand)]
pub enum GetCommand {
    /// Get number of active streams
    #[clap(name = "num_active_streams")]
    NumActiveStreams,

    /// Get default output buffer size
    #[clap(name = "default_output_buffer_size")]
    DefaultOutputBufferSize,

    /// Get current output volume (0 - 100)
    #[clap(name = "output_volume")]
    OutputVolume,

    /// Get DBus introspect xml for interface org.chromium.cras.Control
    #[clap(name = "control_introspect")]
    ControlIntrospect,
}

impl GetCommand {
    fn into_dbus_control(self) -> DBusControlOp {
        use GetCommand::*;
        match self {
            NumActiveStreams => DBusControlOp::GetNumberOfActiveStreams,
            OutputVolume => DBusControlOp::GetOutputVolume,
            DefaultOutputBufferSize => DBusControlOp::GetDefaultOutputBufferSize,
            ControlIntrospect => DBusControlOp::Introspect,
        }
    }

    /// Consumes and executes the GetCommand
    pub fn run(self) -> Result<()> {
        self.into_dbus_control().run().map_err(Error::CrasDBus)
    }
}

#[cfg(test)]
mod tests {
    use clap::Parser;

    use super::*;
    use crate::arguments::Command;

    fn get_command(args: &[&str]) -> std::result::Result<GetCommand, clap::Error> {
        match Command::try_parse_from(args)? {
            Command::Get { command } => Ok(command),
            _ => panic!("not a get command"),
        }
    }

    #[test]
    fn parse_get_command() {
        assert!(get_command(&["cras_tests", "get", "no-such-command"]).is_err());
        assert_eq!(
            get_command(&["cras_tests", "get", "help"])
                .unwrap_err()
                .kind(),
            clap::ErrorKind::DisplayHelp
        );
        assert_eq!(
            get_command(&["cras_tests", "get", "control_introspect"])
                .unwrap()
                .into_dbus_control(),
            DBusControlOp::Introspect
        );
        assert_eq!(
            get_command(&["cras_tests", "get", "num_active_streams"])
                .unwrap()
                .into_dbus_control(),
            DBusControlOp::GetNumberOfActiveStreams
        );
        assert_eq!(
            get_command(&["cras_tests", "get", "default_output_buffer_size"])
                .unwrap()
                .into_dbus_control(),
            DBusControlOp::GetDefaultOutputBufferSize
        );
        assert_eq!(
            get_command(&["cras_tests", "get", "output_volume"])
                .unwrap()
                .into_dbus_control(),
            DBusControlOp::GetOutputVolume
        );
    }
}
