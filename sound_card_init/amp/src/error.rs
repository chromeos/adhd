// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#![allow(missing_docs)]

use dsm::metrics::UMASoundCardInitResult;
use remain::sorted;
use thiserror::Error as ThisError;

use crate::cs35l41;
use crate::max98373d;
use crate::max98390d;
use crate::tas2563;

pub type Result<T> = std::result::Result<T, Error>;

#[sorted]
#[derive(Debug, ThisError)]
pub enum Error {
    #[error(transparent)]
    AlsaCardError(#[from] cros_alsa::CardError),
    #[error(transparent)]
    AlsaControlError(#[from] cros_alsa::ControlError),
    #[error(transparent)]
    AlsaControlTLVError(#[from] cros_alsa::ControlTLVError),
    #[error(transparent)]
    Cs35l41Error(#[from] cs35l41::Error),
    #[error(transparent)]
    DSMError(#[from] dsm::Error),
    #[error(transparent)]
    Max98373Error(#[from] max98373d::Error),
    #[error(transparent)]
    Max98390Error(#[from] max98390d::Error),
    #[error(transparent)]
    SerdeJsonError(#[from] serde_json::Error),
    #[error(transparent)]
    SerdeYamlError(#[from] serde_yaml::Error),
    #[error(transparent)]
    Tas2563Error(#[from] tas2563::Error),
    #[error("unsupported amp: {0}")]
    UnsupportedAmp(String),
}

impl From<&Error> for UMASoundCardInitResult {
    fn from(e: &Error) -> UMASoundCardInitResult {
        match e {
            Error::AlsaCardError(_) => UMASoundCardInitResult::AlsaCardError,
            Error::AlsaControlError(_) => UMASoundCardInitResult::AlsaControlError,
            Error::AlsaControlTLVError(_) => UMASoundCardInitResult::AlsaControlTLVError,
            Error::Cs35l41Error(_) => UMASoundCardInitResult::Cs35l41Error,
            Error::DSMError(_) => UMASoundCardInitResult::DSMError,
            Error::Max98373Error(_) => UMASoundCardInitResult::Max98373Error,
            Error::Max98390Error(_) => UMASoundCardInitResult::Max98390Error,
            Error::SerdeJsonError(_) => UMASoundCardInitResult::SerdeJsonError,
            Error::SerdeYamlError(_) => UMASoundCardInitResult::SerdeYamlError,
            Error::Tas2563Error(_) => UMASoundCardInitResult::Tas2563Error,
            Error::UnsupportedAmp(_) => UMASoundCardInitResult::UnsupportedAmp,
        }
    }
}
