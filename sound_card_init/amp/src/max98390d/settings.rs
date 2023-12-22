// Copyright 2020 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
use std::string::String;

use dsm::RDCRange;
use serde::Deserialize;

use crate::Result;

/// `DeviceSettings` includes the settings of max98390. It currently includes:
/// * the settings of amplifier calibration.
/// * the path of dsm_param.
#[derive(Debug, Default, PartialEq, Deserialize, Clone)]
pub struct DeviceSettings {
    pub amp_calibrations: AmpCalibSettings,
}
#[derive(Debug, Default, PartialEq, Deserialize, Clone)]
pub struct AmpCalibCtrl {
    // Path of the dsm_param.bin file.
    pub dsm_param: String,
    // Mixer control to get/set rdc value.
    pub rdc_ctrl: String,
    // Mixer control to get/set adaptive rdc value.
    pub adaptive_rdc_ctrl: String,
    // Mixer control to get/set ambient temperature value.
    pub temp_ctrl: String,
    // Mixer control to trigger calibration.
    pub calib_ctrl: String,
    // Mixer control to adjust volume for the safe mode.
    // This is used to limit the gain to prevent speaker damage due to faulty amp initialization.
    pub volume_ctrl: String,
    // Volume in the safe mode.
    pub safe_mode_volume: u32,
    // Volume in the normal mode.
    pub normal_mode_volume: u32,
}

/// `AmpCalibSettings` includes the settings needed for amplifier calibration.
#[derive(Debug, Default, PartialEq, Deserialize, Clone)]
pub struct AmpCalibSettings {
    // Mixer control to get/set rdc value.
    pub controls: Vec<AmpCalibCtrl>,
    pub boot_time_calibration_enabled: bool,
    pub rdc_ranges: Vec<RDCRange>,
}

impl AmpCalibSettings {
    /// Returns the number of channels.
    pub fn num_channels(&self) -> usize {
        self.controls.len()
    }
}

impl DeviceSettings {
    /// Creates a `DeviceSettings` from a yaml str.
    pub fn from_yaml_str(conf: &str) -> Result<DeviceSettings> {
        let settings: DeviceSettings = serde_yaml::from_str(conf)
            .map_err(|e| dsm::Error::DeserializationFailed("DeviceSettings".to_owned(), e))?;
        Ok(settings)
    }
}
