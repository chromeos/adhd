// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
use log::info;
/// The utils to create and parse sound_card_init run time file.
use metrics_rs::MetricsLibrary;

/// 'UMACalibrationResult' is the uma enum for calibration result.
#[derive(Clone, Copy)]
pub enum UMACalibrationResult {
    /// Use the previous calibration value due to boot time calibration failed.
    CaibFailedUsePreviousValue,
    /// Use the new calibration value.
    NewCalibrationValue,
    /// Calibration value is not within the speaker acceptance range.
    LargeCalibrationDiff,
    /// Use the previous calibration value.
    UsePreviousValue,
    ///  The max value of the enum.
    Last,
}

impl UMAEnum for UMACalibrationResult {
    fn name(&self) -> &'static str {
        "Cras.SoundCardInit.CalibrationResult"
    }
    fn max(&self) -> i32 {
        UMACalibrationResult::Last as i32
    }
}
impl Into<i32> for UMACalibrationResult {
    fn into(self) -> i32 {
        self as i32
    }
}

/// 'UMASoundCardInitResult' is the uma enum for boot time workflow result.
#[derive(Clone, Copy)]
pub enum UMASoundCardInitResult {
    ///  amp.boot_time_calibration() returns ok.
    OK = 0,
    AlsaCardError,
    AlsaControlError,
    AlsaControlTLVError,
    Cs35l41Error,
    DSMError,
    Max98373Error,
    Max98390Error,
    SerdeJsonError,
    SerdeYamlError,
    UnsupportedAmp,
    ///  The max value of the enum.
    Last,
}

impl UMAEnum for UMASoundCardInitResult {
    fn name(&self) -> &'static str {
        "Cras.SoundCardInit.ExitCode"
    }
    fn max(&self) -> i32 {
        UMASoundCardInitResult::Last as i32
    }
}
impl Into<i32> for UMASoundCardInitResult {
    fn into(self) -> i32 {
        self as i32
    }
}

/// 'UMAWaitForSpeaker' is the uma enum for wait_for_speakers_ready result.
#[derive(Clone, Copy)]
pub enum UMAWaitForSpeaker {
    /// wait_for_speakers_ready returns ok.
    OK,
    /// wait_for_speakers_ready returns error.
    Error,
    ///  The max value of the enum.
    Last,
}

impl UMAEnum for UMAWaitForSpeaker {
    fn name(&self) -> &'static str {
        "Cras.SoundCardInit.WaitForSpeaker"
    }
    fn max(&self) -> i32 {
        UMAWaitForSpeaker::Last as i32
    }
}
impl Into<i32> for UMAWaitForSpeaker {
    fn into(self) -> i32 {
        self as i32
    }
}

pub trait UMAEnum {
    fn name(&self) -> &'static str;
    fn max(&self) -> i32;
}

pub fn log_uma_enum<T: UMAEnum + Into<i32> + Copy>(x: T) {
    let metrics_mutex = MetricsLibrary::get().unwrap();
    let mut metrics = metrics_mutex.lock().unwrap();
    if let Err(e) = metrics.send_enum_to_uma(x.name(), x.into(), x.max()) {
        info!("send_enum_to_uma: {} failed: {}", x.name(), e);
    }
}
