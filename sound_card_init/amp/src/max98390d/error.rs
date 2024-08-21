// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use remain::sorted;
use thiserror::Error as ThisError;

#[sorted]
#[derive(Debug, ThisError)]
pub enum Error {
    #[error("invalid rdc: {0}, rdc acceptant range: [{1},{2}]")]
    InvalidRDC(f32, f32, f32),
    #[error("invalid calibration temperature： {0}, temperature acceptant range: [{1},{2}]")]
    InvalidTemperature(f32, f32, f32),
    #[error("missing dsm_param.bin")]
    MissingDSMParam,
}
