// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
use std::error;

use thiserror::Error as ThisError;

use crate::cras_dbus::{self, DBusControlOp};
use libcras::{CrasIodevNodeId, CrasScreenRotation};

// Errors for set command.
#[derive(ThisError, Debug)]
pub enum Error {
    #[error("Unknown command '{0:}'")]
    UnknownCommand(String),
    #[error("A command must be provided")]
    MissingCommand,
    #[error("failed in cras_dbus: {0:}")]
    CrasDBus(cras_dbus::Error),
    #[error("Missing argument for {0:}")]
    MissingArgument(String),
    #[error("Invalid {0:} argument '{1:}': {2:}")]
    InvalidArgument(String, String, String),
    #[error("Invalid matrix size")]
    InvalidMatrixSize,
}

type Result<T> = std::result::Result<T, Error>;

#[derive(Debug, PartialEq)]
pub enum SetCommand {
    DBusControl(DBusControlOp),
}

fn missing_argument(s: &str) -> Error {
    Error::MissingArgument(s.to_string())
}

fn invalid_argument(cmd: &str, arg: &str, e: &dyn error::Error) -> Error {
    Error::InvalidArgument(cmd.to_string(), arg.to_string(), e.to_string())
}

impl SetCommand {
    /// Parse SetCommand from args.
    pub fn parse<T: AsRef<str>>(
        program_name: &str,
        command_name: &str,
        args: &[T],
    ) -> Result<Option<Self>> {
        let mut args = args.iter().map(|s| s.as_ref());
        let cmd = args.next().ok_or_else(|| {
            Self::show_usage(program_name, command_name);
            Error::MissingCommand
        })?;
        match cmd {
            "help" => Ok(None),
            "output_volume" => {
                let volume = args
                    .next()
                    .ok_or_else(|| missing_argument(cmd))
                    .map(|s| s.parse::<i32>().map_err(|e| invalid_argument(cmd, s, &e)))??;

                Ok(Some(Self::DBusControl(DBusControlOp::SetOutputVolume(
                    volume,
                ))))
            }
            "global_remix" => {
                let num_channels = args
                    .next()
                    .ok_or_else(|| missing_argument(cmd))
                    .map(|s| s.parse::<u32>().map_err(|e| invalid_argument(cmd, s, &e)))??;
                let coefficients = args
                    .map(|s| s.parse::<f64>().map_err(|e| invalid_argument(cmd, s, &e)))
                    .collect::<Result<Vec<_>>>()?;
                if coefficients.len() as u32 != num_channels * num_channels {
                    Err(Error::InvalidMatrixSize)
                } else {
                    Ok(Some(Self::DBusControl(
                        DBusControlOp::SetGlobalOutputChannelRemix(num_channels, coefficients),
                    )))
                }
            }
            "display_rotation" => {
                let node_id = args
                    .next()
                    .ok_or_else(|| missing_argument(cmd))
                    .and_then(|s| {
                        s.parse::<CrasIodevNodeId>()
                            .map_err(|e| invalid_argument(cmd, s, &e))
                    })?;

                let rotation = args
                    .next()
                    .ok_or_else(|| missing_argument(cmd))
                    .and_then(|s| {
                        s.parse::<CrasScreenRotation>()
                            .map_err(|e| invalid_argument(cmd, s, &e))
                    })?;
                Ok(Some(Self::DBusControl(DBusControlOp::SetDisplayRotation(
                    node_id.into(),
                    rotation,
                ))))
            }
            s => Err(Error::UnknownCommand(s.to_string())),
        }
        .map_err(|e| {
            Self::show_usage(program_name, command_name);
            e
        })
    }

    // Consumes and executes the current operation.
    pub fn run(self) -> Result<()> {
        match self {
            Self::DBusControl(op) => op.run().map_err(Error::CrasDBus),
        }
    }

    fn show_usage(program_name: &str, command_name: &str) {
        eprintln!(
            "Usage: {} {} [command] <command args>",
            program_name, command_name,
        );
        let commands = [
            ("help", "", "Print help message"),
            ("", "", ""),
            (
                "output_volume",
                "VOLUME",
                "Set output to the given VOLUME (0 - 100).",
            ),
            (
                "global_remix",
                "NUM_CHANNELS [COEFFICENTS]",
                "Float array representing |num_channels| * |num_channels| matrix.",
            ),
        ];

        for command in &commands {
            let command_string = format!("{} {}", command.0, command.1);
            eprintln!("\t{: <30} {}", command_string, command.2);
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn parse_set_command() {
        assert!(SetCommand::parse("cras_tests", "set", &["no-such-command"]).is_err());
        assert!(SetCommand::parse("cras_tests", "set", &["help"])
            .unwrap()
            .is_none());
        assert!(SetCommand::parse("cras_tests", "set", &["output_volume"]).is_err());
        assert!(SetCommand::parse("cras_tests", "set", &["output_volume", "1a"]).is_err());
        assert_eq!(
            SetCommand::parse("cras_tests", "set", &["output_volume", "50"])
                .unwrap()
                .unwrap(),
            SetCommand::DBusControl(DBusControlOp::SetOutputVolume(50))
        );
        assert!(SetCommand::parse(
            "cras_tests",
            "set",
            &["global_remix", "2", "0.5", "0.5", "0.5"]
        )
        .is_err());
        assert_eq!(
            SetCommand::parse(
                "cras_tests",
                "set",
                &["global_remix", "2", "0.5", "0.5", "0.5", "0.5"]
            )
            .unwrap()
            .unwrap(),
            SetCommand::DBusControl(DBusControlOp::SetGlobalOutputChannelRemix(
                2,
                vec![0.5, 0.5, 0.5, 0.5]
            ))
        );
        assert_eq!(
            SetCommand::parse("cras_tests", "set", &["display_rotation", "7:0", "2"])
                .unwrap()
                .unwrap(),
            SetCommand::DBusControl(DBusControlOp::SetDisplayRotation(
                30064771072,
                CrasScreenRotation::ROTATE_180
            ))
        );
        assert!(
            SetCommand::parse("cras_tests", "set", &["display_rotation", "7:0", "20"]).is_err()
        );
        assert!(
            SetCommand::parse("cras_tests", "set", &["display_rotation", "7:0:1", "2"]).is_err()
        );
    }
}
