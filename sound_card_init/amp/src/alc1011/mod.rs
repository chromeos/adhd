// Copyright 2021 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//! `alc1011` module implements the required initialization workflows for sound
//! cards that use alc1011 smart amp.
#![deny(missing_docs)]
mod settings;

use std::fmt;
use std::fmt::Debug;
use std::fmt::Formatter;
use std::fs;
use std::path::Path;

use cros_alsa::Card;
use cros_alsa::IntControl;
use dsm::CalibData;
use dsm::Error;
use dsm::RDCRange;
use dsm::DSM;
use log::info;
use settings::AmpCalibSettings;
use settings::DeviceSettings;

use crate::Amp;
use crate::Result;

/// It implements the amplifier boot time calibration flow.
pub struct ALC1011 {
    card: Card,
    setting: AmpCalibSettings,
}

/// `ALC1011CalibData` represents the ALC1011 calibration data.
#[derive(Clone, Copy)]
struct ALC1011CalibData {
    /// The calibrated raw value of DC resistance of the speaker.
    pub rdc: i32,
    /// The ambient temperature in celsius unit at which the rdc is measured.
    pub temp: f32,
}

impl CalibData for ALC1011CalibData {
    fn new(rdc: i32, temp: f32) -> Self {
        Self { rdc, temp }
    }

    fn rdc(&self) -> i32 {
        self.rdc
    }

    fn temp(&self) -> f32 {
        self.temp
    }
    /// Converts the calibrated value to real DC resistance in ohm unit.
    #[inline]
    fn rdc_to_ohm(x: i32) -> f32 {
        (1 << 24) as f32 / x as f32
    }
}

impl ALC1011CalibData {
    /// Converts the ambient temperature from celsius to the DSM unit.
    #[inline]
    fn celsius_to_dsm_unit(celsius: f32) -> i32 {
        celsius as i32
    }
}

impl Debug for ALC1011CalibData {
    fn fmt(&self, f: &mut Formatter) -> fmt::Result {
        self.debug_fmt(f)
    }
}

impl Amp for ALC1011 {
    /// Performs ALC1011 boot time calibration.
    ///
    /// # Errors
    ///
    /// If any amplifiers fail to complete the calibration.
    fn boot_time_calibration(&mut self) -> Result<()> {
        if self.setting.boot_time_calibration_enabled {
            return Err(Error::BootTimeCalibrationNotSupported.into());
        }
        let num_channels = self.setting.num_channels();
        let dsm = DSM::new(
            self.card.name(),
            num_channels,
            Self::TEMP_UPPER_LIMIT_CELSIUS,
            Self::TEMP_LOWER_LIMIT_CELSIUS,
        );
        info!("skip boot time calibration and use vpd values");
        dsm.wait_for_speakers_ready()?;
        let calib = dsm.get_all_vpd_calibration_value()?;
        self.apply_calibration_value(&calib)?;
        info!("applied {:?}", calib);
        Ok(())
    }

    fn get_applied_rdc(&mut self, ch: usize) -> Result<f32> {
        if ch >= self.setting.controls.len() {
            return Err(dsm::Error::InvalidChannelNumer(ch).into());
        }

        Ok(ALC1011CalibData::rdc_to_ohm(
            self.card
                .control_by_name::<IntControl>(&self.setting.controls[ch].rdc_ctrl)?
                .get()?,
        ))
    }

    fn num_channels(&mut self) -> usize {
        self.setting.num_channels()
    }

    fn rdc_ranges(&mut self) -> Vec<RDCRange> {
        self.setting.rdc_ranges.clone()
    }
}

impl ALC1011 {
    const TEMP_UPPER_LIMIT_CELSIUS: f32 = 40.0;
    const TEMP_LOWER_LIMIT_CELSIUS: f32 = 0.0;

    /// Creates an `ALC1011`.
    /// # Arguments
    ///
    /// * `card_name` - card_name.
    /// * `config_path` - config file path.
    ///
    /// # Results
    ///
    /// * `ALC1011` - It implements the ALC1011 functions of boot time calibration.
    ///
    /// # Errors
    ///
    /// * If `Card` creation from sound card name fails.
    pub fn new(card_name: &str, config_path: &Path) -> Result<Self> {
        let conf = fs::read_to_string(config_path)
            .map_err(|e| Error::FileIOFailed(config_path.to_path_buf(), e))?;
        let settings = DeviceSettings::from_yaml_str(&conf)?;
        Ok(Self {
            card: Card::new(card_name)?,
            setting: settings.amp_calibrations,
        })
    }

    /// Applies the calibration value to the amp.
    fn apply_calibration_value(&mut self, calib: &[ALC1011CalibData]) -> Result<()> {
        for (ch, &ALC1011CalibData { rdc, temp }) in calib.iter().enumerate() {
            self.card
                .control_by_name::<IntControl>(&self.setting.controls[ch].rdc_ctrl)?
                .set(rdc)?;
            self.card
                .control_by_name::<IntControl>(&self.setting.controls[ch].temp_ctrl)?
                .set(ALC1011CalibData::celsius_to_dsm_unit(temp))?;
        }
        Ok(())
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn celsius_to_dsm_unit() {
        assert_eq!(ALC1011CalibData::celsius_to_dsm_unit(25.0), 25);
    }

    #[test]
    fn rdc_to_ohm() {
        assert_eq!(ALC1011CalibData::rdc_to_ohm(2081255), 8.061106);
    }
}
