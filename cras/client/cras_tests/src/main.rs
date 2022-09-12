// Copyright 2019 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

mod arguments;
mod audio;
mod control;
mod cras_dbus;
mod getter;
mod setter;

use std::error;
use std::fmt;

use clap::Parser;

use crate::arguments::Command;
use crate::audio::{capture, playback};
use crate::control::control;

#[derive(Debug)]
pub enum Error {
    Audio(audio::Error),
    ParseArgs(arguments::Error),
    Control(control::Error),
    Get(getter::Error),
    Set(setter::Error),
}

impl error::Error for Error {}

impl fmt::Display for Error {
    fn fmt(&self, f: &mut fmt::Formatter) -> fmt::Result {
        use Error::*;
        match self {
            Audio(e) => e.fmt(f),
            ParseArgs(e) => write!(f, "Failed to parse arguments: {}", e),
            Control(e) => e.fmt(f),
            Get(e) => e.fmt(f),
            Set(e) => e.fmt(f),
        }
    }
}

type Result<T> = std::result::Result<T, Error>;

fn run() -> Result<()> {
    let command = Command::parse();

    match command {
        Command::Capture(audio_opts) => capture(audio_opts).map_err(Error::Audio),
        Command::Control { command } => control(command).map_err(Error::Control),
        Command::Playback(audio_opts) => playback(audio_opts).map_err(Error::Audio),
        Command::Get { command } => command.run().map_err(Error::Get),
        Command::Set { command } => command.run().map_err(Error::Set),
    }
}

fn main() {
    // Use run() instead of returning a Result from main() so that we can print
    // errors using Display instead of Debug.
    if let Err(e) = run() {
        eprintln!("{}", e);
    }
}
