// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! This mod contains all possible errors that can occur within utils.
use std::fmt;
use std::io;
use std::time;
use std::{error, path::PathBuf};

use remain::sorted;

/// Alias for a `Result` with the error type `utils::Error`.
pub type Result<T> = std::result::Result<T, Error>;

/// This type represents all possible errors that can occur within utils.
#[sorted]
#[derive(Debug)]
pub enum Error {
    /// It wraps file path with the io::Error.
    FileIOFailed(PathBuf, io::Error),
    /// It wraps file path with the serde_yaml::Error.
    SerdeError(PathBuf, serde_yaml::Error),
    /// It wraps time::SystemTimeError.
    SystemTimeError(time::SystemTimeError),
}

impl error::Error for Error {}

impl fmt::Display for Error {
    fn fmt(&self, f: &mut fmt::Formatter) -> fmt::Result {
        use Error::*;
        match self {
            FileIOFailed(file, e) => write!(f, "{:?}: {}", file, e),
            SerdeError(file, e) => write!(f, "{:?}: {}", file, e),
            SystemTimeError(e) => write!(f, "{}", e),
        }
    }
}
