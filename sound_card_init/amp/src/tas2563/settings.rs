// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
use std::string::String;

use dsm::RDCRange;
use serde::Deserialize;

use crate::Result;

/// `DeviceSettings` includes the settings of tas2563. It currently includes:
/// * the settings of amplifier calibration.
/// * the path of dsm_param.
#[derive(Debug, Default, PartialEq, Deserialize, Clone)]
pub struct DeviceSettings {
    pub amp_calibrations: AmpCalibSettings,
}
#[derive(Debug, Default, PartialEq, Deserialize, Clone)]
pub struct AmpCalibCtrl {
    /// The mixer control name of `Speaker Calibrated Data`. Ex: "Speaker Calibrated Data".
    pub cal_data: String,
}

/// `AmpCalibSettings` includes the settings needed for amplifier calibration.
#[derive(Debug, Default, PartialEq, Deserialize, Clone)]
pub struct AmpCalibSettings {
    // Mixer control to get/set rdc value.
    pub controls: Vec<AmpCalibCtrl>,
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
