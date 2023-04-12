// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//`cs35l41` module implements the required initialization workflows for sound
// cards that use cs35l41 smart amp.
// It currently applies the calibration results for cs35l41.
#![deny(missing_docs)]
mod error;
mod settings;

use std::convert::TryFrom;
use std::fmt;
use std::fs;
use std::path::Path;
use std::thread::sleep;
use std::time::Duration;

use cros_alsa::elem::Elem;
use cros_alsa::Card;
use cros_alsa::Control;
use cros_alsa::ControlOps;
use cros_alsa::Ctl;
use cros_alsa::ElemId;
use cros_alsa::SimpleEnumControl;
use cros_alsa::SwitchControl;
use dsm::CalibData;
use dsm::RDCRange;
use dsm::ZeroPlayer;
use dsm::DSM;
pub use error::Error;
use log::info;
use settings::AmpCalibCtrl;
use settings::AmpCalibSettings;
use settings::DeviceSettings;

use crate::Amp;
use crate::Result;

const FIRMWARE_TYPE_PROTECTION: &str = "Protection";

const PCM_SOURCE_DSP: &str = "DSP";

#[derive(PartialEq, Clone, Copy)]
enum PreLoadSwitch {
    On = 1,
    Off = 0,
}

#[derive(PartialEq, Clone, Copy)]
enum CalibStatus {
    Success = 1,
}
enum CalibApplyStatus {
    // No data available or corrupted data or value is out of range;
    // using default impedance threshold.
    Corrupted,
    // The calibration data has been successfully updated
    Success,
    // The checksum did not match; using default impedance threshold
    Mismatched,
}

impl TryFrom<i32> for CalibApplyStatus {
    type Error = Error;
    fn try_from(x: i32) -> std::result::Result<Self, Self::Error> {
        match x {
            1 => Ok(CalibApplyStatus::Corrupted),
            2 => Ok(CalibApplyStatus::Success),
            3 => Ok(CalibApplyStatus::Mismatched),
            _ => Err(Error::InvalidCalibApplyStatus(x)),
        }
    }
}

#[derive(ControlOps)]
pub struct FourBytesControl<'a> {
    handle: &'a mut Ctl,
    id: ElemId,
}

impl<'a> Control<'a> for FourBytesControl<'a> {
    type Item = [u8; 4];
    fn new(handle: &'a mut Ctl, id: ElemId) -> Self {
        Self { handle, id }
    }
}

impl<'a> FourBytesControl<'a> {
    pub fn get(&mut self) -> Result<i32> {
        let data = self.load()?;
        Ok(i32::from_be_bytes(data))
    }

    pub fn set(&mut self, val: i32) -> Result<()> {
        self.save(val.to_be_bytes())?;
        Ok(())
    }
}

/// It implements the CS35L41 functions of boot time calibration.
#[derive(Debug)]
pub struct CS35L41 {
    card: Card,
    setting: AmpCalibSettings,
}

/// `CS35L41CalibData` represents the CS35L41 calibration data.
/// #[derive(Clone, Copy)]
struct CS35L41CalibData {
    /// The calibrated raw value of DC resistance of the speaker.
    pub rdc: i32,
    /// The ambient temperature in celsius unit at which the rdc is measured.
    pub temp: f32,
}

impl CalibData for CS35L41CalibData {
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
        x as f32 * 5.857_143_4 / 8192.0
    }
}

impl fmt::Debug for CS35L41CalibData {
    fn fmt(&self, f: &mut fmt::Formatter) -> fmt::Result {
        write!(f, "rdc: {} ohm", self.rdc_ohm(),)
    }
}

impl Amp for CS35L41 {
    /// Performs CS35L41 boot time calibration.
    ///
    /// # Errors
    ///
    /// If the amplifier fails to complete the calibration.
    fn boot_time_calibration(&mut self) -> Result<()> {
        if self.setting.boot_time_calibration_enabled {
            return Err(dsm::Error::BootTimeCalibrationNotSupported.into());
        }
        for setting in &self.setting.controls {
            if !Path::new(&setting.firmware_file).exists() {
                return Err(Error::MissingFirmwareFile(
                    Path::new(&setting.firmware_file).to_owned(),
                )
                .into());
            }
            if !Path::new(&setting.tuning_file).exists() {
                return Err(
                    Error::MissingTuningFile(Path::new(&setting.tuning_file).to_owned()).into(),
                );
            }
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
        self.verify_calibration_applied()?;
        Ok(())
    }

    fn get_applied_rdc(&mut self, ch: usize) -> Result<f32> {
        if ch >= self.setting.controls.len() {
            return Err(dsm::Error::InvalidChannelNumer(ch).into());
        }

        let rdc = self
            .card
            .control_by_name::<FourBytesControl>(&self.setting.controls[ch].cal_r)?
            .get()?;

        Ok(CS35L41CalibData::rdc_to_ohm(rdc))
    }

    fn num_channels(&mut self) -> usize {
        self.setting.num_channels()
    }

    fn rdc_ranges(&mut self) -> Vec<RDCRange> {
        self.setting.rdc_ranges.clone()
    }
}

impl CS35L41 {
    const TEMP_UPPER_LIMIT_CELSIUS: f32 = 40.0;
    const TEMP_LOWER_LIMIT_CELSIUS: f32 = 0.0;
    const CALIB_APPLY_TIME: Duration = Duration::from_millis(100);
    // Sleeps for FRIMWARE_LOADING_TIME between the
    // firmware loading and the writing to "DSP1 Protection cd CAL_*" controls
    // to fix the failure when applying the calibration result.
    const FRIMWARE_LOADING_TIME: Duration = Duration::from_millis(10);

    /// Creates an `CS35L41`.
    /// # Arguments
    ///
    /// * `card_name` - card name.
    /// * `config_path` - config file path.
    ///
    /// # Results
    ///
    /// * `CS35L41` - It implements the CS35L41 functions of boot time calibration.
    ///
    /// # Errors
    ///
    /// * If `Card` creation from sound card name fails.
    pub fn new(card_name: &str, config_path: &Path) -> Result<Self> {
        let conf = fs::read_to_string(config_path)
            .map_err(|e| dsm::Error::FileIOFailed(config_path.to_path_buf(), e))?;
        let settings = DeviceSettings::from_yaml_str(&conf)?;
        Ok(Self {
            card: Card::new(card_name)?,
            setting: settings.amp_calibrations,
        })
    }

    /// Applies the calibration value to the amp.
    fn apply_calibration_value(&mut self, calib: &[CS35L41CalibData]) -> Result<()> {
        for (ch, &CS35L41CalibData { rdc, temp }) in calib.iter().enumerate() {
            CS35L41::load_firmware(&mut self.card, &self.setting.controls[ch])?;

            self.card
                .control_by_name::<FourBytesControl>(&self.setting.controls[ch].cal_ambient)?
                .set(temp as i32)?;

            self.card
                .control_by_name::<FourBytesControl>(&self.setting.controls[ch].cal_r)?
                .set(rdc)?;
            self.card
                .control_by_name::<FourBytesControl>(&self.setting.controls[ch].cal_status)?
                .set(CalibStatus::Success as i32)?;
            self.card
                .control_by_name::<FourBytesControl>(&self.setting.controls[ch].cal_checksum)?
                .set(rdc + CalibStatus::Success as i32)?;
        }

        // Apply calibration result to CSPL by playing music, which causes CSPL to begin running.
        // When CSPL starts running, it updates the calibration resistance using applied values.
        let mut zero_player: ZeroPlayer = Default::default();
        zero_player.start(Self::CALIB_APPLY_TIME)?;
        zero_player.stop()?;

        Ok(())
    }

    fn load_firmware(card: &mut Card, setting: &AmpCalibCtrl) -> Result<()> {
        card.control_by_name::<SwitchControl>(&setting.preload_switch)?
            .off()?;
        card.control_by_name::<SimpleEnumControl>(&setting.firmware_type)?
            .set_by_name(FIRMWARE_TYPE_PROTECTION)?;
        card.control_by_name::<SimpleEnumControl>(&setting.pcm_source)?
            .set_by_name(PCM_SOURCE_DSP)?;
        card.control_by_name::<SwitchControl>(&setting.preload_switch)?
            .on()?;
        card.control_by_name::<SwitchControl>(&setting.preload_switch)?
            .state()?;

        sleep(CS35L41::FRIMWARE_LOADING_TIME);
        Ok(())
    }

    //Verifies calibration values are correctly applied.
    fn verify_calibration_applied(&mut self) -> Result<()> {
        for setting in &self.setting.controls {
            let status: i32 = self
                .card
                .control_by_name::<FourBytesControl>(&setting.cal_set_status)?
                .get()?;
            match CalibApplyStatus::try_from(status)? {
                CalibApplyStatus::Success => continue,
                _ => {
                    return Err(Error::ApplyCalibrationFailed(status).into());
                }
            }
        }
        Ok(())
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    #[test]
    fn rdc_to_ohm() {
        assert_eq!(CS35L41CalibData::rdc_to_ohm(4779), 3.4169054);
    }
}
