// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use remain::sorted;
use thiserror::Error as ThisError;

#[sorted]
#[derive(Debug, ThisError)]
pub enum Error {
    #[error("failed to apply calibration results. status: {0}")]
    ApplyCalibrationFailed(i32),
    #[error("invalid CalibApplyStatus: {0}")]
    InvalidCalibApplyStatus(i32),
    #[error("invalid Channel Number: {0}")]
    InvalidChannelNumber(i32),
    #[error("invalid VPD Byte Count {0}")]
    InvalidVPDByteCount(i32),
}
