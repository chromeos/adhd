// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
use std::string::String;

use dsm::RDCRange;
use serde::Deserialize;

use crate::Result;

/// `DeviceSettings` includes the settings of cs35l41. It currently includes:
/// * the settings of amplifier calibration.
/// * the path of dsm_param.
#[derive(Debug, Default, PartialEq, Deserialize, Clone)]
pub struct DeviceSettings {
    pub amp_calibrations: AmpCalibSettings,
}
#[derive(Debug, Default, PartialEq, Deserialize, Clone)]
pub struct AmpCalibCtrl {
    /// The path of the firmware file. Ex: "/lib/firmware/cs35l41-dsp1-spk-prot.wmfw".
    pub firmware_file: String,
    /// The path of the tuning file. Ex: "/lib/firmware/cs35l41-dsp1-spk-prot.bin";
    pub tuning_file: String,
    /// The mixer control name of `DSP1 Preload Switch`. Ex: "Left DSP1 Preload Switch".
    pub preload_switch: String,
    /// The mixer control name of `DSP1 Firmware`. Ex: "Left DSP1 Firmware".
    pub firmware_type: String,
    /// The mixer control name of `PCM Source`. Ex: "Left PCM Source".
    pub pcm_source: String,
    /// The mixer control name of `DSP1 Protection cd CAL_AMBIENT`.
    /// Ex: "Left DSP1 Protection cd CAL_AMBIENT".
    pub cal_ambient: String,
    /// The mixer control name of `DSP1 Protection cd CAL_R`. Ex: "Left DSP1 Protection cd CAL_R".
    pub cal_r: String,
    /// The mixer control name of `DSP1 Protection cd CAL_STATUS`.
    /// Ex: "Left DSP1 Protection cd CAL_STATUS".
    pub cal_status: String,
    /// The mixer control name of `DSP1 Protection cd CAL_CHECKSUM`.
    /// Ex: "Left DSP1 Protection cd CAL_CHECKSUM".
    pub cal_checksum: String,
    /// The mixer control name of `DSP1 Protection cd CAL_SET_STATUS`.
    /// Ex: "Left DSP1 Protection cd CAL_SET_STATUS".
    pub cal_set_status: String,
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
