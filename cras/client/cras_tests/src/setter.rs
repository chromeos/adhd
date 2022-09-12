// Copyright 2021 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
use thiserror::Error as ThisError;

use crate::cras_dbus::{self, DBusControlOp};
use libcras::{CrasIodevNodeId, CrasScreenRotation};

// Errors for set command.
#[derive(ThisError, Debug)]
pub enum Error {
    #[error("failed in cras_dbus: {0:}")]
    CrasDBus(cras_dbus::Error),
    #[error("Invalid matrix size")]
    InvalidMatrixSize,
}

type Result<T> = std::result::Result<T, Error>;

#[derive(PartialEq, Debug, clap::Subcommand)]
pub enum SetCommand {
    /// Set output to the given VOLUME (0 - 100)
    #[clap(name = "output_volume")]
    OutputVolume { volume: i32 },

    #[clap(name = "global_remix")]
    GlobalRemix {
        num_channels: u32,
        /// Float array representing |num_channels| * |num_channels| matrix
        coefficients: Vec<f64>,
    },

    /// Sets the display rotation for the given node
    #[clap(name = "display_rotation")]
    DisplayRotation {
        node_id: CrasIodevNodeId,
        rotation: CrasScreenRotation,
    },

    /// Enable or disable using Floss as Bluetooth stack
    #[clap(name = "floss_enabled")]
    FlossEnabled {
        #[clap(parse(try_from_str))]
        enabled: bool,
    },
}

impl SetCommand {
    fn try_into_dbus_control(self) -> Result<DBusControlOp> {
        use SetCommand::*;
        match self {
            OutputVolume { volume } => Ok(DBusControlOp::SetOutputVolume(volume)),
            GlobalRemix {
                num_channels,
                coefficients,
            } => {
                if coefficients.len() as u32 != num_channels * num_channels {
                    Err(Error::InvalidMatrixSize)
                } else {
                    Ok(DBusControlOp::SetGlobalOutputChannelRemix(
                        num_channels,
                        coefficients,
                    ))
                }
            }
            DisplayRotation { node_id, rotation } => {
                Ok(DBusControlOp::SetDisplayRotation(node_id.into(), rotation))
            }
            FlossEnabled { enabled } => Ok(DBusControlOp::SetFlossEnabled(enabled)),
        }
    }

    /// Consumes and executes the SetCommand
    pub fn run(self) -> Result<()> {
        self.try_into_dbus_control()?.run().map_err(Error::CrasDBus)
    }
}

#[cfg(test)]
mod tests {
    use clap::Parser;

    use super::*;
    use crate::arguments::Command;

    fn set_command(args: &[&str]) -> std::result::Result<SetCommand, clap::Error> {
        match Command::try_parse_from(args)? {
            Command::Set { command } => Ok(command),
            _ => panic!("not a set command"),
        }
    }

    #[test]
    fn parse_set_command() {
        assert!(set_command(&["cras_tests", "set", "no-such-command"]).is_err());
        assert_eq!(
            set_command(&["cras_tests", "set", "help"])
                .unwrap_err()
                .kind(),
            clap::ErrorKind::DisplayHelp
        );
        assert!(set_command(&["cras_tests", "set", "output_volume"]).is_err());
        assert!(set_command(&["cras_tests", "set", "output_volume", "1a"]).is_err());
        assert_eq!(
            set_command(&["cras_tests", "set", "output_volume", "50"])
                .unwrap()
                .try_into_dbus_control()
                .unwrap(),
            DBusControlOp::SetOutputVolume(50)
        );
        assert!(set_command(&[
            "cras_tests",
            "set",
            "global_remix",
            "2",
            "0.5",
            "0.5",
            "0.5"
        ])
        .unwrap()
        .try_into_dbus_control()
        .is_err());
        assert_eq!(
            set_command(&[
                "cras_tests",
                "set",
                "global_remix",
                "2",
                "0.5",
                "0.5",
                "0.5",
                "0.5"
            ])
            .unwrap()
            .try_into_dbus_control()
            .unwrap(),
            DBusControlOp::SetGlobalOutputChannelRemix(2, vec![0.5, 0.5, 0.5, 0.5])
        );
        assert_eq!(
            set_command(&["cras_tests", "set", "display_rotation", "7:0", "2"])
                .unwrap()
                .try_into_dbus_control()
                .unwrap(),
            DBusControlOp::SetDisplayRotation(30064771072, CrasScreenRotation::ROTATE_180)
        );
        assert!(set_command(&["cras_tests", "set", "display_rotation", "7:0", "20"]).is_err());
        assert!(set_command(&["cras_tests", "set", "display_rotation", "7:0:1", "2"]).is_err());
        assert_eq!(
            set_command(&["cras_tests", "set", "floss_enabled", "true"])
                .unwrap()
                .try_into_dbus_control()
                .unwrap(),
            DBusControlOp::SetFlossEnabled(true)
        );
        assert!(set_command(&["cras_tests", "set", "floss_enabled", "3"]).is_err());
    }
}
