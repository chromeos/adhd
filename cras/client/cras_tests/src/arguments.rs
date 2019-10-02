// Copyright 2019 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
use std::error;
use std::fmt;
use std::path::PathBuf;

use audio_streams::SampleFormat;
use getopts::{self, Matches, Options};

#[derive(Debug)]
pub enum Error {
    GetOpts(getopts::Fail),
    InvalidArgument(String, String, String),
    MissingArgument(String),
    MissingCommand,
    MissingFilename,
    UnknownCommand(String),
}

impl error::Error for Error {}

impl fmt::Display for Error {
    fn fmt(&self, f: &mut fmt::Formatter) -> fmt::Result {
        use Error::*;
        match self {
            GetOpts(e) => write!(f, "Getopts Error: {}", e),
            InvalidArgument(flag, value, error_msg) => {
                write!(f, "Invalid {} argument '{}': {}", flag, value, error_msg)
            }
            MissingArgument(subcommand) => write!(f, "Missing argument for {}", subcommand),
            MissingCommand => write!(f, "A command must be provided"),
            MissingFilename => write!(f, "A file name must be provided"),
            UnknownCommand(s) => write!(f, "Unknown command '{}'", s),
        }
    }
}

type Result<T> = std::result::Result<T, Error>;

/// The different types of commands that can be given to cras_tests.
/// Any options for those commands are passed as parameters to the enum values.
#[derive(Debug, PartialEq)]
pub enum Command {
    Capture(AudioOptions),
    Playback(AudioOptions),
    Control(ControlCommand),
}

impl Command {
    pub fn parse<T: AsRef<str>>(args: &[T]) -> Result<Option<Self>> {
        let program_name = args.get(0).map(|s| s.as_ref()).unwrap_or("cras_tests");
        let remaining_args = args.get(2..).unwrap_or(&[]);
        match args.get(1).map(|s| s.as_ref()) {
            None => {
                show_usage(program_name);
                Err(Error::MissingCommand)
            }
            Some("help") => {
                show_usage(program_name);
                Ok(None)
            }
            Some("capture") => Ok(
                AudioOptions::parse(program_name, "capture", remaining_args)?.map(Command::Capture),
            ),
            Some("playback") => Ok(
                AudioOptions::parse(program_name, "playback", remaining_args)?
                    .map(Command::Playback),
            ),
            Some("control") => {
                Ok(ControlCommand::parse(program_name, remaining_args)?.map(Command::Control))
            }
            Some(s) => {
                show_usage(program_name);
                Err(Error::UnknownCommand(s.to_string()))
            }
        }
    }
}

fn show_usage(program_name: &str) {
    eprintln!("Usage: {} [command] <command args>", program_name);
    eprintln!("\nCommands:\n");
    eprintln!("capture - Capture to a file from CRAS");
    eprintln!("playback - Playback to CRAS from a file");
    eprintln!("control - Get and set server settings");
    eprintln!("\nhelp - Print help message");
}

fn show_audio_command_usage(program_name: &str, command: &str, opts: &Options) {
    let brief = format!("Usage: {} {} [options] [filename]", program_name, command);
    eprint!("{}", opts.usage(&brief));
}

/// The possible command line options that can be passed to the 'playback' and
/// 'capture' commands. Optional values will be `Some(_)` only if a value was
/// explicitly provided by the user.
///
/// This struct will be passed to `playback()` and `capture()`.
#[derive(Debug, PartialEq)]
pub struct AudioOptions {
    pub file_name: PathBuf,
    pub buffer_size: Option<usize>,
    pub num_channels: Option<usize>,
    pub format: Option<SampleFormat>,
    pub frame_rate: Option<usize>,
}

fn get_usize_param(matches: &Matches, option_name: &str) -> Result<Option<usize>> {
    matches.opt_get::<usize>(option_name).map_err(|e| {
        let argument = matches.opt_str(option_name).unwrap_or_default();
        Error::InvalidArgument(option_name.to_string(), argument, e.to_string())
    })
}

impl AudioOptions {
    fn parse<T: AsRef<str>>(
        program_name: &str,
        command_name: &str,
        args: &[T],
    ) -> Result<Option<Self>> {
        let mut opts = Options::new();
        opts.optopt("b", "buffer_size", "Buffer size in frames", "SIZE")
            .optopt("c", "channels", "Number of channels", "NUM")
            .optopt(
                "f",
                "format",
                "Sample format (U8, S16_LE, S24_LE, or S32_LE)",
                "FORMAT",
            )
            .optopt("r", "rate", "Audio frame rate (Hz)", "RATE")
            .optflag("h", "help", "Print help message");

        let args = args.iter().map(|s| s.as_ref());
        let matches = match opts.parse(args) {
            Ok(m) => m,
            Err(e) => {
                show_audio_command_usage(program_name, command_name, &opts);
                return Err(Error::GetOpts(e));
            }
        };
        if matches.opt_present("h") {
            show_audio_command_usage(program_name, command_name, &opts);
            return Ok(None);
        }
        let file_name = match matches.free.get(0) {
            None => {
                show_audio_command_usage(program_name, command_name, &opts);
                return Err(Error::MissingFilename);
            }
            Some(file_name) => PathBuf::from(file_name),
        };
        let buffer_size = get_usize_param(&matches, "buffer_size")?;
        let num_channels = get_usize_param(&matches, "channels")?;
        let frame_rate = get_usize_param(&matches, "rate")?;
        let format = match matches.opt_str("format").as_ref().map(|s| s.as_str()) {
            Some("U8") => Some(SampleFormat::U8),
            Some("S16_LE") => Some(SampleFormat::S16LE),
            Some("S24_LE") => Some(SampleFormat::S24LE),
            Some("S32_LE") => Some(SampleFormat::S32LE),
            Some(s) => {
                show_audio_command_usage(program_name, command_name, &opts);
                return Err(Error::InvalidArgument(
                    "format".to_string(),
                    s.to_string(),
                    "Format must be 'U8', 'S16_LE', 'S24_LE', or 'S32_LE'".to_string(),
                ));
            }
            None => None,
        };

        Ok(Some(AudioOptions {
            file_name,
            buffer_size,
            num_channels,
            format,
            frame_rate,
        }))
    }
}

fn show_control_command_usage(program_name: &str) {
    eprintln!("Usage: {} control [command] <command args>", program_name);
    eprintln!("");
    eprintln!("Commands:");
    let commands = [
        ("help", "", "Print help message"),
        ("", "", ""),
        ("get_volume", "", "Get the system volume (0 - 100)"),
        (
            "set_volume",
            "VOLUME",
            "Set the system volume to VOLUME (0 - 100)",
        ),
        ("get_mute", "", "Get the system mute state (true or false)"),
        (
            "set_mute",
            "MUTE",
            "Set the system mute state to MUTE (true or false)",
        ),
    ];
    for command in &commands {
        let command_string = format!("{} {}", command.0, command.1);
        eprintln!("\t{: <20} {}", command_string, command.2);
    }
}

#[derive(Debug, PartialEq)]
pub enum ControlCommand {
    GetSystemVolume,
    SetSystemVolume(u32),
    GetSystemMute,
    SetSystemMute(bool),
}

impl ControlCommand {
    fn parse<T: AsRef<str>>(program_name: &str, args: &[T]) -> Result<Option<Self>> {
        let mut args = args.iter().map(|s| s.as_ref());
        match args.next() {
            Some("help") => {
                show_control_command_usage(program_name);
                Ok(None)
            }
            Some("get_volume") => Ok(Some(ControlCommand::GetSystemVolume)),
            Some("set_volume") => {
                let volume_str = args
                    .next()
                    .ok_or_else(|| Error::MissingArgument("set_volume".to_string()))?;

                let volume = volume_str.parse::<u32>().map_err(|e| {
                    Error::InvalidArgument(
                        "set_volume".to_string(),
                        volume_str.to_string(),
                        e.to_string(),
                    )
                })?;

                Ok(Some(ControlCommand::SetSystemVolume(volume)))
            }
            Some("get_mute") => Ok(Some(ControlCommand::GetSystemMute)),
            Some("set_mute") => {
                let mute_str = args
                    .next()
                    .ok_or_else(|| Error::MissingArgument("set_mute".to_string()))?;

                let mute = mute_str.parse::<bool>().map_err(|e| {
                    Error::InvalidArgument(
                        "set_mute".to_string(),
                        mute_str.to_string(),
                        e.to_string(),
                    )
                })?;
                Ok(Some(ControlCommand::SetSystemMute(mute)))
            }
            Some(s) => {
                show_control_command_usage(program_name);
                Err(Error::UnknownCommand(s.to_string()))
            }
            None => {
                show_control_command_usage(program_name);
                Err(Error::MissingCommand)
            }
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn parse_command() {
        let command = Command::parse(&["cras_tests", "playback", "output.wav"])
            .unwrap()
            .unwrap();
        assert_eq!(
            command,
            Command::Playback(AudioOptions {
                file_name: PathBuf::from("output.wav"),
                frame_rate: None,
                num_channels: None,
                format: None,
                buffer_size: None,
            })
        );

        let command = Command::parse(&["cras_tests", "capture", "input.flac"])
            .unwrap()
            .unwrap();
        assert_eq!(
            command,
            Command::Capture(AudioOptions {
                file_name: PathBuf::from("input.flac"),
                frame_rate: None,
                num_channels: None,
                format: None,
                buffer_size: None,
            })
        );

        let command = Command::parse(&[
            "cras_tests",
            "playback",
            "-r",
            "44100",
            "output.wav",
            "-c",
            "2",
        ])
        .unwrap()
        .unwrap();
        assert_eq!(
            command,
            Command::Playback(AudioOptions {
                file_name: PathBuf::from("output.wav"),
                frame_rate: Some(44100),
                num_channels: Some(2),
                format: None,
                buffer_size: None,
            })
        );

        assert!(Command::parse(&["cras_tests"]).is_err());
        assert!(Command::parse(&["cras_tests", "capture"]).is_err());
        assert!(Command::parse(&["cras_tests", "playback"]).is_err());
        assert!(Command::parse(&["cras_tests", "loopback"]).is_err());
        assert!(Command::parse(&["cras_tests", "loopback", "file.ogg"]).is_err());
        assert!(Command::parse(&["cras_tests", "filename.wav"]).is_err());
        assert!(Command::parse(&["cras_tests", "filename.wav", "capture"]).is_err());
        assert!(Command::parse(&["cras_tests", "help"]).is_ok());
        assert!(Command::parse(&[
            "cras_tests",
            "-c",
            "2",
            "playback",
            "output.wav",
            "-r",
            "44100"
        ])
        .is_err());
    }
}
