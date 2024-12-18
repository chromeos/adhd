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

/// `AmpCalibSettings` includes the settings needed for amplifier calibration.
#[derive(Debug, Default, PartialEq, Deserialize, Clone)]
pub struct AmpCalibSettings {
    /// The mixer control name of `Speaker Calibrated Data`. Ex: "Speaker Calibrated Data".
    pub cal_data: String,
    /// The mixer control name for forcing firmware load.
    /// Ex: "Speaker Force Firmware Load".
    pub force_firmware_load: String,
    /// The mixer control name for setting Speaker Profile.
    /// Ex: "Speaker Profile Id".
    pub speaker_profile: String,
    /// The mixer control name for setting Speaker Config.
    /// Ex: "Speaker Config Id".
    pub speaker_config: String,
    /// The mixer control name for setting Speaker Program.
    /// Ex: "Speaker Program Id".
    pub speaker_program: String,
    /// The profile Id for regular playback mode.
    pub playback_profile_id: i32,
    /// The profile Id for calibration mode.
    pub calibration_profile_id: i32,
    pub rdc_ranges: Vec<RDCRange>,
}

impl AmpCalibSettings {
    /// Returns the number of channels.
    pub fn num_channels(&self) -> usize {
        self.rdc_ranges.len()
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
