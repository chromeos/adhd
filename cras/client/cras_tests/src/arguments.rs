// Copyright 2019 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
use std::fmt;
use std::path::PathBuf;
use std::str::FromStr;

use audio_streams::SampleFormat;
use clap::Args;
use clap::Parser;
use clap::Subcommand;
use clap::ValueEnum;
use thiserror::Error as ThisError;

use crate::getter::GetCommand;
use crate::setter::SetCommand;

#[derive(ThisError, Debug)]
pub enum Error {
    #[error("Invalid file extension '{0:}'. Supported types are 'wav' and 'raw'")]
    InvalidFiletype(String),
}

type Result<T> = std::result::Result<T, Error>;

#[derive(PartialEq, Debug, Parser)]
pub enum Command {
    /// Capture to a file from CRAS
    Capture(AudioOptions),

    /// Playback to CRAS from a file
    Playback(AudioOptions),

    /// Get and set server settings
    Control {
        #[clap(subcommand)]
        command: ControlCommand,
    },

    /// Get server status or settings
    Get {
        #[clap(subcommand)]
        command: GetCommand,
    },

    /// Set server status or settings
    Set {
        #[clap(subcommand)]
        command: SetCommand,
    },
}

#[derive(Debug, PartialEq, Clone, ValueEnum)]
pub enum FileType {
    Raw,
    Wav,
}

impl fmt::Display for FileType {
    fn fmt(&self, f: &mut fmt::Formatter) -> fmt::Result {
        match self {
            FileType::Raw => write!(f, "raw data"),
            FileType::Wav => write!(f, "WAVE"),
        }
    }
}

/// The possible command line options that can be passed to the 'playback' and
/// 'capture' commands. Optional values will be `Some(_)` only if a value was
/// explicitly provided by the user.
///
/// This struct will be passed to `playback()` and `capture()`.
#[derive(Debug, PartialEq, Clone, ValueEnum)]
pub enum LoopbackType {
    #[value(name = "pre_dsp")]
    PreDsp,
    #[value(name = "post_dsp")]
    PostDsp,
}

#[derive(PartialEq, Debug, Args)]
pub struct AudioOptions {
    pub file_name: PathBuf,

    /// Buffer size in frames
    #[arg(short = 'b', long = "buffer_size")]
    pub buffer_size: Option<usize>,

    /// Number of channels
    #[arg(short = 'c', long = "channels")]
    pub num_channels: Option<usize>,

    /// Sample format
    #[arg(short = 'f', long, value_enum)]
    pub format: Option<SampleFormatArg>,

    /// Audio frame rate (Hz)
    #[arg(short = 'r', long = "rate")]
    pub frame_rate: Option<u32>,

    /// Capture from loopback device
    #[arg(long = "loopback", value_enum)]
    pub loopback_type: Option<LoopbackType>,

    /// Type of the file. Defaults to file extension.
    #[arg(long = "file_type", value_enum)]
    pub file_type_option: Option<FileType>,

    /// Duration of playing/recording action in seconds.
    #[arg(short = 'd', long = "duration")]
    pub duration_sec: Option<usize>,

    /// Capture effects to enable.
    #[arg(long = "effects", value_parser = parse_hex_or_decimal)]
    pub effects: Option<u32>,
}

/// Parse string starting with 0x as hex and others as decimal.
fn parse_hex_or_decimal(maybe_hex_string: &str) -> std::result::Result<u32, String> {
    if let Some(hex_string) = maybe_hex_string.strip_prefix("0x") {
        u32::from_str_radix(hex_string, 16)
    } else if let Some(hex_string) = maybe_hex_string.strip_prefix("0X") {
        u32::from_str_radix(hex_string, 16)
    } else {
        u32::from_str(maybe_hex_string)
    }
    .map_err(|e| format!("invalid numeric value {}: {}", maybe_hex_string, e))
}

impl AudioOptions {
    /// Returns the file type specified by the user, or
    /// the file type derived from file_name.
    pub fn file_type(&self) -> Result<FileType> {
        match &self.file_type_option {
            Some(ty) => Ok(ty.clone()),
            None => {
                let extension = self
                    .file_name
                    .extension()
                    .map(|s| s.to_string_lossy().into_owned());
                match extension.as_deref() {
                    Some("wav") | Some("wave") => Ok(FileType::Wav),
                    Some("raw") | None => Ok(FileType::Raw),
                    Some(extension) => Err(Error::InvalidFiletype(extension.to_string())),
                }
            }
        }
    }
}

#[derive(PartialEq, Debug, Copy, Clone, ValueEnum)]
pub enum SampleFormatArg {
    #[value(name = "U8")]
    U8,

    #[value(name = "S16_LE")]
    S16LE,

    #[value(name = "S24_LE")]
    S24LE,

    #[value(name = "S32_LE")]
    S32LE,
}

impl SampleFormatArg {
    pub fn to_sample_format(self) -> SampleFormat {
        use SampleFormatArg::*;
        match self {
            U8 => SampleFormat::U8,
            S16LE => SampleFormat::S16LE,
            S24LE => SampleFormat::S24LE,
            S32LE => SampleFormat::S32LE,
        }
    }
}

#[derive(PartialEq, Debug, Subcommand)]
pub enum ControlCommand {
    /// Get the system volume (0 - 100)
    #[command(name = "get_volume")]
    GetSystemVolume,

    /// Set the system volume to VOLUME (0 - 100)
    #[command(name = "set_volume")]
    SetSystemVolume { volume: u32 },

    /// Get the system mute state (true or false)
    #[command(name = "get_mute")]
    GetSystemMute,

    /// Set the system mute state to MUTE (true or false)
    #[command(name = "set_mute")]
    SetSystemMute {
        #[arg(value_parser = bool::from_str, action = clap::ArgAction::Set)]
        mute: bool,
    },

    /// Print list of output devices
    #[command(name = "list_output_devices")]
    ListOutputDevices {
        /// Print as JSON
        #[arg(long)]
        json: bool,
    },

    /// Print list of input devices
    #[command(name = "list_input_devices")]
    ListInputDevices {
        /// Print as JSON
        #[arg(long)]
        json: bool,
    },

    /// Print list of output nodes
    #[command(name = "list_output_nodes")]
    ListOutputNodes,

    /// Print list of input nodes
    #[command(name = "list_input_nodes")]
    ListInputNodes,

    /// Print stream info, device info
    #[command(name = "dump_audio_debug_info")]
    DumpAudioDebugInfo {
        /// Print as JSON
        #[arg(long)]
        json: bool,
    },
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn parse_command() {
        let command = Command::parse_from(["cras_tests", "playback", "output.wav"]);
        assert_eq!(
            command,
            Command::Playback(AudioOptions {
                file_name: PathBuf::from("output.wav"),
                loopback_type: None,
                file_type_option: None,
                frame_rate: None,
                num_channels: None,
                format: None,
                buffer_size: None,
                duration_sec: None,
                effects: None,
            })
        );

        let command = Command::parse_from(["cras_tests", "capture", "input.raw"]);
        assert_eq!(
            command,
            Command::Capture(AudioOptions {
                file_name: PathBuf::from("input.raw"),
                loopback_type: None,
                file_type_option: None,
                frame_rate: None,
                num_channels: None,
                format: None,
                buffer_size: None,
                duration_sec: None,
                effects: None,
            })
        );

        let command = Command::parse_from([
            "cras_tests",
            "playback",
            "-r",
            "44100",
            "output.wave",
            "-c",
            "2",
        ]);
        assert_eq!(
            command,
            Command::Playback(AudioOptions {
                file_name: PathBuf::from("output.wave"),
                loopback_type: None,
                file_type_option: None,
                frame_rate: Some(44100),
                num_channels: Some(2),
                format: None,
                buffer_size: None,
                duration_sec: None,
                effects: None,
            })
        );

        let command = Command::parse_from([
            "cras_tests",
            "playback",
            "-r",
            "44100",
            "output",
            "-c",
            "2",
            "-d",
            "5",
        ]);
        assert_eq!(
            command,
            Command::Playback(AudioOptions {
                file_name: PathBuf::from("output"),
                loopback_type: None,
                file_type_option: None,
                frame_rate: Some(44100),
                num_channels: Some(2),
                format: None,
                buffer_size: None,
                duration_sec: Some(5),
                effects: None,
            })
        );

        let command = Command::parse_from([
            "cras_tests",
            "capture",
            "rec.raw",
            "--duration",
            "10",
            "--rate",
            "48000",
        ]);
        assert_eq!(
            command,
            Command::Capture(AudioOptions {
                file_name: PathBuf::from("rec.raw"),
                loopback_type: None,
                file_type_option: None,
                frame_rate: Some(48000),
                num_channels: None,
                format: None,
                buffer_size: None,
                duration_sec: Some(10),
                effects: None,
            })
        );

        let command = Command::parse_from(["cras_tests", "capture", "out.wav", "--effects=0x11"]);
        assert_eq!(
            command,
            Command::Capture(AudioOptions {
                file_name: PathBuf::from("out.wav"),
                loopback_type: None,
                file_type_option: None,
                frame_rate: None,
                num_channels: None,
                format: None,
                buffer_size: None,
                duration_sec: None,
                effects: Some(0x11),
            })
        );

        assert!(Command::try_parse_from(["cras_tests"]).is_err());
        assert!(Command::try_parse_from(["cras_tests", "capture"]).is_err());
        assert!(Command::try_parse_from(["cras_tests", "playback"]).is_err());
        assert!(Command::try_parse_from(["cras_tests", "loopback"]).is_err());
        assert!(Command::try_parse_from(["cras_tests", "loopback", "filename.wav"]).is_err());
        assert!(Command::try_parse_from(["cras_tests", "filename.wav"]).is_err());
        assert!(Command::try_parse_from(["cras_tests", "filename.wav", "capture"]).is_err());
        assert_eq!(
            Command::try_parse_from(["cras_tests", "help"])
                .unwrap_err()
                .kind(),
            clap::error::ErrorKind::DisplayHelp
        );
        assert!(Command::try_parse_from([
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

    fn parse_file_type(args: &[&str]) -> Result<FileType> {
        match Command::parse_from(args) {
            Command::Playback(options) | Command::Capture(options) => options.file_type(),
            _ => panic!("not playback/capture command"),
        }
    }

    #[test]
    fn file_type() {
        assert_eq!(
            parse_file_type(&["cras_tests", "playback", "output.wav"]).unwrap(),
            FileType::Wav
        );
        assert_eq!(
            parse_file_type(&["cras_tests", "capture", "input.raw"]).unwrap(),
            FileType::Raw
        );
        assert_eq!(
            parse_file_type(&[
                "cras_tests",
                "playback",
                "-r",
                "44100",
                "output.wave",
                "-c",
                "2",
            ])
            .unwrap(),
            FileType::Wav
        );
        assert!(matches!(
            parse_file_type(&["cras_tests", "capture", "input.mp3"]).unwrap_err(),
            Error::InvalidFiletype(_)
        ));
        assert!(matches!(
            parse_file_type(&["cras_tests", "capture", "input.ogg"]).unwrap_err(),
            Error::InvalidFiletype(_)
        ));
        assert!(matches!(
            parse_file_type(&["cras_tests", "capture", "input.flac"]).unwrap_err(),
            Error::InvalidFiletype(_)
        ));
    }
}
