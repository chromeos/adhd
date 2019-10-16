// Copyright 2019 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
use std::error;
use std::fmt;
use std::num::ParseIntError;
use std::path::PathBuf;

use getopts::{self, Matches, Options};

#[derive(Debug)]
pub enum Error {
    GetOpts(getopts::Fail),
    InvalidArgument(String, String, ParseIntError),
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
            InvalidArgument(flag, value, e) => {
                write!(f, "Invalid {} argument '{}': {}", flag, value, e)
            }
            MissingCommand => write!(f, "A command must be provided"),
            MissingFilename => write!(f, "A file name must be provided"),
            UnknownCommand(s) => write!(f, "Unknown command '{}'", s),
        }
    }
}

type Result<T> = std::result::Result<T, Error>;

#[derive(Debug, PartialEq)]
pub enum Command {
    Capture,
    Playback,
}

impl fmt::Display for Command {
    fn fmt(&self, f: &mut fmt::Formatter) -> fmt::Result {
        match self {
            Command::Capture => write!(f, "capture"),
            Command::Playback => write!(f, "playback"),
        }
    }
}

fn show_usage(program_name: &str) {
    eprintln!("Usage: {} [command] <command args>", program_name);
    eprintln!("\nCommands:\n");
    eprintln!("capture - Capture to a file from CRAS");
    eprintln!("playback - Playback to CRAS from a file");
    eprintln!("\nhelp - Print help message");
}

fn show_command_usage(program_name: &str, command: &Command, opts: &Options) {
    let brief = format!("Usage: {} {} [options] [filename]", program_name, command);
    eprint!("{}", opts.usage(&brief));
}

pub struct AudioOptions {
    pub command: Command,
    pub file_name: PathBuf,
    pub buffer_size: Option<usize>,
    pub num_channels: Option<usize>,
    pub frame_rate: Option<usize>,
}

fn get_usize_param(matches: &Matches, option_name: &str) -> Result<Option<usize>> {
    matches.opt_get::<usize>(option_name).map_err(|e| {
        let argument = matches.opt_str(option_name).unwrap_or_default();
        Error::InvalidArgument(option_name.to_string(), argument, e)
    })
}

impl AudioOptions {
    pub fn parse_from_args<T: AsRef<str>>(args: &[T]) -> Result<Option<Self>> {
        let mut opts = Options::new();
        opts.optopt("b", "buffer_size", "Buffer size in frames", "SIZE")
            .optopt("c", "channels", "Number of channels", "NUM")
            .optopt("r", "rate", "Audio frame rate (Hz)", "RATE")
            .optflag("h", "help", "Print help message");

        let mut args = args.into_iter().map(|s| s.as_ref());

        let program_name = args.next().unwrap_or("cras_tests");
        let command = match args.next() {
            None => {
                show_usage(program_name);
                return Err(Error::MissingCommand);
            }
            Some("help") => {
                show_usage(program_name);
                return Ok(None);
            }
            Some("capture") => Command::Capture,
            Some("playback") => Command::Playback,
            Some(s) => {
                show_usage(program_name);
                return Err(Error::UnknownCommand(s.to_string()));
            }
        };

        let matches = match opts.parse(args) {
            Ok(m) => m,
            Err(e) => {
                show_command_usage(program_name, &command, &opts);
                return Err(Error::GetOpts(e));
            }
        };
        if matches.opt_present("h") {
            show_command_usage(program_name, &command, &opts);
            return Ok(None);
        }
        let file_name = match matches.free.get(0) {
            None => {
                show_command_usage(program_name, &command, &opts);
                return Err(Error::MissingFilename);
            }
            Some(file_name) => PathBuf::from(file_name),
        };
        let buffer_size = get_usize_param(&matches, "buffer_size")?;
        let num_channels = get_usize_param(&matches, "channels")?;
        let frame_rate = get_usize_param(&matches, "rate")?;

        Ok(Some(AudioOptions {
            command,
            file_name,
            buffer_size,
            num_channels,
            frame_rate,
        }))
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use std::ffi::OsString;

    #[test]
    fn parse_from_args() {
        let opts = AudioOptions::parse_from_args(&["cras_tests", "playback", "output.wav"])
            .unwrap()
            .unwrap();
        assert_eq!(opts.command, Command::Playback);
        assert_eq!(opts.file_name, OsString::from("output.wav"));
        assert_eq!(opts.frame_rate, None);
        assert_eq!(opts.num_channels, None);
        assert_eq!(opts.buffer_size, None);

        let opts = AudioOptions::parse_from_args(&["cras_tests", "capture", "input.flac"])
            .unwrap()
            .unwrap();
        assert_eq!(opts.command, Command::Capture);
        assert_eq!(opts.file_name, OsString::from("input.flac"));
        assert_eq!(opts.frame_rate, None);
        assert_eq!(opts.num_channels, None);
        assert_eq!(opts.buffer_size, None);

        let opts = AudioOptions::parse_from_args(&[
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
        assert_eq!(opts.command, Command::Playback);
        assert_eq!(opts.file_name, OsString::from("output.wav"));
        assert_eq!(opts.frame_rate, Some(44100));
        assert_eq!(opts.num_channels, Some(2));
        assert_eq!(opts.buffer_size, None);

        assert!(AudioOptions::parse_from_args(&["cras_tests"]).is_err());
        assert!(AudioOptions::parse_from_args(&["cras_tests", "capture"]).is_err());
        assert!(AudioOptions::parse_from_args(&["cras_tests", "playback"]).is_err());
        assert!(AudioOptions::parse_from_args(&["cras_tests", "loopback"]).is_err());
        assert!(AudioOptions::parse_from_args(&["cras_tests", "loopback", "file.ogg"]).is_err());
        assert!(AudioOptions::parse_from_args(&["cras_tests", "filename.wav"]).is_err());
        assert!(AudioOptions::parse_from_args(&["cras_tests", "filename.wav", "capture"]).is_err());
        assert!(AudioOptions::parse_from_args(&["cras_tests", "help"]).is_ok());
        assert!(AudioOptions::parse_from_args(&[
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
