use thiserror::Error as ThisError;

use crate::cras_dbus::{self, DBusControlOp};

// Errors for set command.
#[derive(ThisError, Debug)]
pub enum Error {
    #[error("Unknown command '{0:}'")]
    UnknownCommand(String),
    #[error("A command must be provided")]
    MissingCommand,
    #[error("faild in cras_dbus: {0:}")]
    CrasDBus(cras_dbus::Error),
    #[error("Missing argument for {0:}")]
    MissingArgument(String),
    #[error("Invalid {0:} argument '{1:}': {2:}")]
    InvalidArgument(String, String, String),
}

type Result<T> = std::result::Result<T, Error>;

#[derive(Debug, PartialEq)]
pub enum SetCommand {
    DBusControl(DBusControlOp),
}

impl SetCommand {
    /// Parse SetCommand from args.
    pub fn parse<T: AsRef<str>>(
        program_name: &str,
        command_name: &str,
        args: &[T],
    ) -> Result<Option<Self>> {
        let mut args = args.iter().map(|s| s.as_ref());
        match args.next() {
            Some("help") => {
                Self::show_usage(program_name, command_name);
                Ok(None)
            }
            Some("output_volume") => {
                let volume_str = args
                    .next()
                    .ok_or_else(|| Error::MissingArgument("output_volume".to_string()))?;

                let volume = volume_str.parse::<i32>().map_err(|e| {
                    Error::InvalidArgument(
                        "output_volume".to_string(),
                        volume_str.to_string(),
                        e.to_string(),
                    )
                })?;
                Ok(Some(Self::DBusControl(DBusControlOp::SetOutputVolume(
                    volume,
                ))))
            }
            Some(s) => {
                Self::show_usage(program_name, command_name);
                Err(Error::UnknownCommand(s.to_string()))
            }
            None => {
                Self::show_usage(program_name, command_name);
                Err(Error::MissingCommand)
            }
        }
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
    }
}
