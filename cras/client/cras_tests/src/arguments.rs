// Copyright 2019 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
use std::fmt;
use std::path::PathBuf;

use audio_streams::SampleFormat;
use clap::{ArgEnum, Args, Parser, Subcommand};
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
#[clap(global_setting(clap::AppSettings::DeriveDisplayOrder))]
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

#[derive(Debug, PartialEq, Clone, ArgEnum)]
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
#[derive(Debug, PartialEq, Clone, ArgEnum)]
pub enum LoopbackType {
    #[clap(name = "pre_dsp")]
    PreDsp,
    #[clap(name = "post_dsp")]
    PostDsp,
}

#[derive(PartialEq, Debug, Args)]
pub struct AudioOptions {
    pub file_name: PathBuf,

    /// Buffer size in frames
    #[clap(short = 'b', long = "buffer_size")]
    pub buffer_size: Option<usize>,

    /// Number of channels
    #[clap(short = 'c', long = "channels")]
    pub num_channels: Option<usize>,

    /// Sample format
    #[clap(short = 'f', long, arg_enum)]
    pub format: Option<SampleFormatArg>,

    /// Audio frame rate (Hz)
    #[clap(short = 'r', long = "rate")]
    pub frame_rate: Option<u32>,

    /// Capture from loopback device
    #[clap(long = "loopback", arg_enum)]
    pub loopback_type: Option<LoopbackType>,

    /// Type of the file. Defaults to file extension.
    #[clap(long = "file_type", arg_enum)]
    pub file_type_option: Option<FileType>,
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

#[derive(PartialEq, Debug, Copy, Clone, ArgEnum)]
pub enum SampleFormatArg {
    #[clap(name = "U8")]
    U8,

    #[clap(name = "S16_LE")]
    S16LE,

    #[clap(name = "S24_LE")]
    S24LE,

    #[clap(name = "S32_LE")]
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
    #[clap(name = "get_volume")]
    GetSystemVolume,

    /// Set the system volume to VOLUME (0 - 100)
    #[clap(name = "set_volume")]
    SetSystemVolume { volume: u32 },

    /// Get the system mute state (true or false)
    #[clap(name = "get_mute")]
    GetSystemMute,

    /// Set the system mute state to MUTE (true or false)
    #[clap(name = "set_mute")]
    SetSystemMute {
        #[clap(parse(try_from_str))]
        mute: bool,
    },

    /// Print list of output devices
    #[clap(name = "list_output_devices")]
    ListOutputDevices,

    /// Print list of input devices
    #[clap(name = "list_input_devices")]
    ListInputDevices,

    /// Print list of output nodes
    #[clap(name = "list_output_nodes")]
    ListOutputNodes,

    /// Print list of input nodes
    #[clap(name = "list_input_nodes")]
    ListInputNodes,

    /// Print stream info, device info
    #[clap(name = "dump_audio_debug_info")]
    DumpAudioDebugInfo {
        /// Print as JSON
        #[clap(long)]
        json: bool,
    },
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn parse_command() {
        let command = Command::parse_from(&["cras_tests", "playback", "output.wav"]);
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
            })
        );

        let command = Command::parse_from(&["cras_tests", "capture", "input.raw"]);
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
            })
        );

        let command = Command::parse_from(&[
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
            })
        );

        let command =
            Command::parse_from(&["cras_tests", "playback", "-r", "44100", "output", "-c", "2"]);
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
            })
        );

        assert!(Command::try_parse_from(&["cras_tests"]).is_err());
        assert!(Command::try_parse_from(&["cras_tests", "capture"]).is_err());
        assert!(Command::try_parse_from(&["cras_tests", "playback"]).is_err());
        assert!(Command::try_parse_from(&["cras_tests", "loopback"]).is_err());
        assert!(Command::try_parse_from(&["cras_tests", "loopback", "filename.wav"]).is_err());
        assert!(Command::try_parse_from(&["cras_tests", "filename.wav"]).is_err());
        assert!(Command::try_parse_from(&["cras_tests", "filename.wav", "capture"]).is_err());
        assert_eq!(
            Command::try_parse_from(&["cras_tests", "help"])
                .unwrap_err()
                .kind(),
            clap::ErrorKind::DisplayHelp
        );
        assert!(Command::try_parse_from(&[
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
