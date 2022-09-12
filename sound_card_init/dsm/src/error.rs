// Copyright 2020 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
use std::any::Any;
use std::io;
use std::num::ParseIntError;
use std::path::PathBuf;
use std::sync::PoisonError;
use std::time;

use remain::sorted;
use thiserror::Error as ThisError;

use crate::CalibData;

pub type Result<T> = std::result::Result<T, Error>;

#[sorted]
#[derive(Debug, ThisError)]
pub enum Error {
    #[error("boot time calibration is not supported")]
    BootTimeCalibrationNotSupported,
    #[error("calibration is not finished in time")]
    CalibrationTimeout,
    #[error(transparent)]
    CrasClientFailed(#[from] libcras::Error),
    #[error("failed to parse {0}: {1}")]
    DeserializationFailed(String, serde_yaml::Error),
    #[error("{0:?}: {1}")]
    FileIOFailed(PathBuf, io::Error),
    #[error("internal speaker is not found in cras")]
    InternalSpeakerNotFound,
    #[error("invalid channel number: {0}")]
    InvalidChannelNumer(usize),
    #[error("invalid datastore format")]
    InvalidDatastore,
    #[error("invalid shutdown time")]
    InvalidShutDownTime,
    #[error("calibration difference is too large, calib: {0:?}")]
    LargeCalibrationDiff(Box<dyn CalibData>),
    #[error("mutex is poisoned")]
    MutexPoisonError,
    #[error("{0}")]
    NewPlayStreamFailed(libcras::BoxError),
    #[error("{0}")]
    NextPlaybackBufferFailed(libcras::BoxError),
    #[error(transparent)]
    PlaybackFailed(#[from] io::Error),
    #[error("{0}： {1}")]
    SerdeError(PathBuf, serde_yaml::Error),
    #[error("playback is not started in time")]
    StartPlaybackTimeout,
    #[error(transparent)]
    SystemTimeError(#[from] time::SystemTimeError),
    #[error("failed to parse vpd {0}: {1}")]
    VPDParseFailed(String, ParseIntError),
    #[error("run_play_zero_worker panics: {0:?}")]
    WorkerPanics(Box<dyn Any + Send + 'static>),
    #[error("zero player is not running")]
    ZeroPlayerIsNotRunning,
    #[error("zero player is running")]
    ZeroPlayerIsRunning,
}

impl<T> From<PoisonError<T>> for Error {
    fn from(_: PoisonError<T>) -> Error {
        Error::MutexPoisonError
    }
}
