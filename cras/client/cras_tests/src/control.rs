// Copyright 2019 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use std::error;
use std::fmt;

use libcras::CrasClient;

use crate::arguments::ControlCommand;

/// An enumeration of errors that can occur when running `ControlCommand` using
/// the `control()` function.
#[derive(Debug)]
pub enum Error {
    Libcras(libcras::Error),
}

impl error::Error for Error {}

impl fmt::Display for Error {
    fn fmt(&self, f: &mut fmt::Formatter) -> fmt::Result {
        use Error::*;
        match self {
            Libcras(e) => write!(f, "Libcras Error: {}", e),
        }
    }
}

type Result<T> = std::result::Result<T, Error>;

/// Connect to CRAS and run the given `ControlCommand`.
pub fn control(command: ControlCommand) -> Result<()> {
    use ControlCommand::*;
    let mut cras_client = CrasClient::new().map_err(Error::Libcras)?;
    match command {
        GetSystemVolume => println!("{}", cras_client.get_system_volume()),
        SetSystemVolume(volume) => {
            cras_client
                .set_system_volume(volume)
                .map_err(Error::Libcras)?;
        }
        GetSystemMute => println!("{}", cras_client.get_system_mute()),
        SetSystemMute(mute) => {
            cras_client.set_system_mute(mute).map_err(Error::Libcras)?;
        }
    };
    Ok(())
}
