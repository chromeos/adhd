// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use remain::sorted;
use thiserror::Error as ThisError;

#[sorted]
#[derive(Debug, ThisError, PartialEq)]
pub enum Error {
    #[error("failed to update DsmParam, err: {0}")]
    DSMParamUpdateFailed(cros_alsa::ControlTLVError),
    #[error("invalid dsm param from kcontrol")]
    InvalidDSMParam,
    #[error("invalid rdc: {0}, rdc acceptant range: [{1},{2}]")]
    InvalidRDC(f32, f32, f32),
    #[error("invalid calibration temperature： {0}, temperature acceptant range: [{1},{2}]")]
    InvalidTemperature(f32, f32, f32),
    #[error("missing dsm_param.bin")]
    MissingDSMParam,
}
