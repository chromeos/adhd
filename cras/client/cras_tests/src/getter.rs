use thiserror::Error as ThisError;

use crate::cras_dbus::{self, DBusControlOp};

// Errors for get command.
#[derive(ThisError, Debug)]
pub enum Error {
    #[error("Unknown command '{0:}'")]
    UnknownCommand(String),
    #[error("A command must be provided")]
    MissingCommand,
    #[error("faild in cras_dbus: {0:}")]
    CrasDBus(cras_dbus::Error),
}

type Result<T> = std::result::Result<T, Error>;

#[derive(Debug, PartialEq)]
pub enum GetCommand {
    DBusControl(DBusControlOp),
}

impl GetCommand {
    pub fn parse<T: AsRef<str>>(
        program_name: &str,
        command_name: &str,
        args: &[T],
    ) -> Result<Option<Self>> {
        match args.get(0).map(|s| s.as_ref()) {
            Some("help") => {
                Self::show_usage(program_name, command_name);
                Ok(None)
            }
            Some("num_active_streams") => Ok(Some(Self::DBusControl(
                DBusControlOp::GetNumberOfActiveStreams,
            ))),
            Some("default_output_buffer_size") => Ok(Some(Self::DBusControl(
                DBusControlOp::GetDefaultOutputBufferSize,
            ))),
            Some("control_introspect") => Ok(Some(Self::DBusControl(DBusControlOp::Introspect))),
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
            ("num_active_streams", "", "Get number of active streams"),
            (
                "default_output_buffer_size",
                "",
                "Get default output buffer size",
            ),
            (
                "control_introspect",
                "",
                "Get DBus introspect xml for interface org.chromium.cras.Control",
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
    fn parse_get_command() {
        assert!(GetCommand::parse("cras_tests", "get", &["no-such-command"]).is_err());
        assert!(GetCommand::parse("cras_tests", "get", &["help"])
            .unwrap()
            .is_none());
        assert_eq!(
            GetCommand::parse("cras_tests", "get", &["control_introspect"])
                .unwrap()
                .unwrap(),
            GetCommand::DBusControl(DBusControlOp::Introspect)
        );
        assert_eq!(
            GetCommand::parse("cras_tests", "get", &["num_active_streams"])
                .unwrap()
                .unwrap(),
            GetCommand::DBusControl(DBusControlOp::GetNumberOfActiveStreams)
        );
        assert_eq!(
            GetCommand::parse("cras_tests", "get", &["default_output_buffer_size"])
                .unwrap()
                .unwrap(),
            GetCommand::DBusControl(DBusControlOp::GetDefaultOutputBufferSize)
        );
    }
}
