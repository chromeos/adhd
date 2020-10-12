// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
use std::string::String;

use serde::Deserialize;

use crate::error::{Error, Result};

/// `DeviceSettings` includes the settings of max98390. It currently includes:
/// * the settings of amplifier calibration.
/// * the path of dsm_param.
#[derive(Debug, Default, PartialEq, Deserialize, Clone)]
pub struct DeviceSettings {
    pub amp_calibrations: Vec<AmpCalibSettings>,
    pub dsm_param: String,
}

/// `AmpCalibSettings` includes the settings needed for amplifier calibration.
#[derive(Debug, Default, PartialEq, Deserialize, Clone)]
pub struct AmpCalibSettings {
    /// `AmpSettings`.
    pub amp: AmpSettings,
    /// Vpd file of Rdc.
    pub rdc_vpd: String,
    /// Vpd file of ambient temperature.
    pub temp_vpd: String,
    /// File to store the boot time calibration values.
    pub calib_file: String,
}

/// `AmpSettings` represents mixer control names and amp params needed for amplifier calibration.
#[derive(Debug, Default, PartialEq, Deserialize, Clone)]
pub struct AmpSettings {
    // Mixer control to get/set rdc value.
    pub rdc_ctrl: String,
    // Mixer control to get/set ambient temperature value.
    pub temp_ctrl: String,
    // Mixer control to trigger calibration.
    pub calib_ctrl: String,
    // Mixer control to adjust volume.
    pub volume_ctrl: String,
    // The threshold to put volume into normal.
    pub volume_high_limit: i32,
    // The threshold to put volume into protected mode.
    pub volume_low_limit: i32,
    // The upper limit of a valid temperature value.
    pub temp_upper_limit: i32,
    // The lower limit of a valid temperature value.
    pub temp_lower_limit: i32,
}

impl DeviceSettings {
    /// Creates a `DeviceSettings` from a yaml str.
    pub fn from_yaml_str(conf: &str) -> Result<DeviceSettings> {
        let settings: DeviceSettings = serde_yaml::from_str(conf)
            .map_err(|e| Error::DeserializationFailed("DeviceSettings".to_owned(), e))?;
        Ok(settings)
    }
}
