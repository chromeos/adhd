// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//`tas2563` module implements the required initialization workflows for sound
// cards that use tas2563 smart amp.
// It currently applies the calibration results for tas2563.
#![deny(missing_docs)]
mod error;
mod settings;

use std::fmt;
use std::fs;
use std::path::Path;
use std::time::Duration;

use cros_alsa::elem::Elem;
use cros_alsa::Card;
use cros_alsa::Control;
use cros_alsa::ControlOps;
use cros_alsa::Ctl;
use cros_alsa::ElemId;
use cros_alsa::IntControl;
use dsm::vpd;
use dsm::vpd::Tas2563VPD;
use dsm::CalibData;
use dsm::RDCRange;
use dsm::ZeroPlayer;
use dsm::DSM;
pub use error::Error;
use log::info;
use settings::AmpCalibSettings;
use settings::DeviceSettings;

use crate::Amp;
use crate::Result;

/// Amp Config ID mode used by set_mode().
enum ConfigMode {
    /// Regular playback.
    Playback,
    /// Config should be set to this before updating Speaker Calibrated Data.
    Calibration,
}

/// The calibration values in `Speaker Calibrated Data` are in the following
/// format:
/// Data length (1 byte)
/// ID (1 byte)
/// register_array_address (15 bytes)
/// dev id0 (1 byte)
/// - PRM_R0_REG (4 bytes)
/// - PRM_R0_LOW_REG (4 bytes)
/// - PRM_INVR0_REG (4 bytes)
/// - PRM_POW_REG (4 bytes)
/// - PRM_TLIMIT_REG (4 bytes)
/// dev id1 (1 byte)
/// - ...
/// Therefore, if device has two channels, there are 59 bytes. If the device has
/// 8 channels, then there are 101 bytes.

/// `TwoChannelsControl` that reads and writes a multi-byte entry for TAS2563
/// speaker calibrated data.
#[derive(ControlOps)]
pub struct TwoChannelsControl<'a> {
    handle: &'a mut Ctl,
    id: ElemId,
}
impl<'a> Control<'a> for TwoChannelsControl<'a> {
    type Item = [u8; 59];
    fn new(handle: &'a mut Ctl, id: ElemId) -> Self {
        Self { handle, id }
    }
}

impl<'a> TwoChannelsControl<'a> {
    pub fn get(&mut self) -> Result<[u8; 59]> {
        let data = self.load()?;
        Ok(data)
    }

    pub fn set(&mut self, data: &[u8]) -> Result<()> {
        self.save(data.try_into().unwrap())?;
        Ok(())
    }
}

/// `FourChannelsControl` that reads and writes a multi-byte entry for TAS2563
/// speaker calibrated data.
#[derive(ControlOps)]
pub struct FourChannelsControl<'a> {
    handle: &'a mut Ctl,
    id: ElemId,
}
impl<'a> Control<'a> for FourChannelsControl<'a> {
    type Item = [u8; 101];
    fn new(handle: &'a mut Ctl, id: ElemId) -> Self {
        Self { handle, id }
    }
}

impl<'a> FourChannelsControl<'a> {
    pub fn get(&mut self) -> Result<[u8; 101]> {
        let data = self.load()?;
        Ok(data)
    }

    pub fn set(&mut self, data: &[u8]) -> Result<()> {
        self.save(data.try_into().unwrap())?;
        Ok(())
    }
}

/// It implements the TAS2563 functions of boot time calibration.
#[derive(Debug)]
pub struct TAS2563 {
    card: Card,
    setting: AmpCalibSettings,
}

/// `TAS2563CalibData` represents the TAS2563 calibration data.
/// #[derive(Clone, Copy)]
struct TAS2563CalibData {
    /// The speaker impedance at ambient temperature during silent playback.
    pub r0: u32,
    /// r0_low = r0 * (1 + alpha*(-60))
    /// alpha is the temperature coefficient alpha (1/K)
    /// Default alpha is 0.0039
    pub r0_low: u32,
    /// The inverse of r0
    pub invr0: u32,
    /// Total RMS power coefficient
    pub rms_pow: u32,
    /// Delta temperature limit coefficient
    pub tlimit: u32,
    // The id and register addresses
    pub register_array: Vec<u8>,
}

impl CalibData for TAS2563CalibData {
    type VPDType = Tas2563VPD;

    fn rdc(&self) -> i32 {
        self.r0.try_into().unwrap()
    }

    // TAS2563 only cares about ambient temperature at calibration, and doesn't
    // continue to track it.
    // Use a
    fn temp(&self) -> f32 {
        20.0
    }

    /// Converts the calibrated value to real DC resistance in ohm unit.
    #[inline]
    fn rdc_to_ohm(x: i32) -> f32 {
        x as f32 / (0x8000000u32) as f32
    }

    /// Converts the value from ohm unit to the DSM unit (VPD format).
    #[inline]
    fn ohm_to_rdc(x: f32) -> i32 {
        (x as f32 * (0x8000000u32) as f32).round() as i32
    }
}

impl TAS2563CalibData {
    /// The function to return the r0 in ohm unit.
    fn r0_ohm(&self) -> f32
    where
        Self: Sized,
    {
        Self::rdc_to_ohm(self.rdc())
    }

    fn rms_pow(&self) -> u32 {
        self.rms_pow
    }

    /// Converts the calibrated value to real power in watts.
    #[inline]
    fn pow_to_watt(x: u32) -> f32 {
        x as f32 / (0x8000000u32) as f32
    }
    /// Converts the value from watts unit to the DSM unit (VPD format).
    #[inline]
    fn watt_to_pow(x: f32) -> u32 {
        (x as f32 * (0x8000000u32) as f32).round() as u32
    }

    /// The function to return the rms_pow in watt unit.
    fn rms_pow_watt(&self) -> f32
    where
        Self: Sized,
    {
        Self::pow_to_watt(self.rms_pow())
    }

    fn tlimit(&self) -> u32 {
        self.tlimit
    }

    /// Converts the calibrated value to real temperature in Celsius.
    #[inline]
    fn temperature_to_cel(x: u32) -> f32 {
        x as f32 / (0x8000000u32) as f32
    }

    /// Converts the value from Celsius unit to the DSM unit (VPD format).
    #[inline]
    fn cel_to_temperature(x: f32) -> u32 {
        (x as f32 * (0x8000000u32) as f32).round() as u32
    }

    /// The function to return the tlimit in Celsius unit.
    fn tlimit_celsius(&self) -> f32
    where
        Self: Sized,
    {
        Self::temperature_to_cel(self.rms_pow())
    }
}

impl fmt::Debug for TAS2563CalibData {
    fn fmt(&self, f: &mut fmt::Formatter) -> fmt::Result {
        write!(
            f,
            "( r0: {} ohm, RMS power: {} W, delta thermal limit: {} C)",
            self.r0_ohm(),
            self.rms_pow_watt(),
            self.tlimit_celsius(),
        )
    }
}

impl From<Tas2563VPD> for TAS2563CalibData {
    fn from(vpd: Tas2563VPD) -> Self {
        Self {
            r0: u32::from_be_bytes(vpd.dsm_calib_value[1..5].try_into().unwrap()),
            r0_low: u32::from_be_bytes(vpd.dsm_calib_value[5..9].try_into().unwrap()),
            invr0: u32::from_be_bytes(vpd.dsm_calib_value[9..13].try_into().unwrap()),
            rms_pow: u32::from_be_bytes(vpd.dsm_calib_value[13..17].try_into().unwrap()),
            tlimit: u32::from_be_bytes(vpd.dsm_calib_value[17..21].try_into().unwrap()),
            register_array: vpd.dsm_calib_register_array,
        }
    }
}

impl Amp for TAS2563 {
    /// Performs TAS2563 boot time calibration.
    ///
    /// # Errors
    ///
    /// If the amplifier fails to complete the calibration.
    fn boot_time_calibration(&mut self) -> Result<()> {
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
        if ch >= self.setting.num_channels() {
            return Err(dsm::Error::InvalidChannelNumber(ch).into());
        }

        info!("Get applied rdc channel {}", ch);
        let data = match self.setting.rdc_ranges.len() {
            2 => {
                &(self
                    .card
                    .control_by_name::<TwoChannelsControl>(&self.setting.cal_data)?
                    .get()?)[..]
            }
            4 => {
                &(self
                    .card
                    .control_by_name::<FourChannelsControl>(&self.setting.cal_data)?
                    .get()?)[..]
            }
            _ => {
                return Err(Error::InvalidChannelNumber(
                    self.setting.num_channels().try_into().unwrap(),
                )
                .into())
            }
        };
        let start_location =
            Self::HEADER_LENGTH + Self::CHANNEL_DATA_LENGTH * ch + Self::R0_LOCATION;
        let rdc = u32::from_be_bytes(data[start_location..start_location + 4].try_into().unwrap());
        Ok(TAS2563CalibData::rdc_to_ohm(rdc.try_into().unwrap()))
    }

    /// Get the fake dsm_calib_r0 value by channel index.
    fn get_fake_r0(&mut self, _ch: usize) -> i32 {
        0
    }

    fn set_rdc(&mut self, _ch: usize, _rdc: f32) -> Result<()> {
        Ok(())
    }

    /// Set the temp value by channel index.
    fn set_temp(&mut self, _ch: usize, _temp: f32) -> Result<()> {
        Ok(())
    }

    fn num_channels(&mut self) -> usize {
        self.setting.num_channels()
    }

    fn rdc_ranges(&mut self) -> Vec<RDCRange> {
        self.setting.rdc_ranges.clone()
    }

    fn set_safe_mode(&mut self, enable: bool) -> Result<()> {
        info!("set_safe_mode: {}", enable);
        if enable {
            self.set_profile(ConfigMode::Playback);
        }
        Ok(())
    }

    /// Get an example vpd value by channel index.
    /// It is used for auto-repair job in lab testing.
    /// Implementation of TAS2563 writes the default values as VPD
    fn set_fake_vpd(&mut self) -> Result<()> {
        // Make default value readable by writing fake value in playback mode.
        let mut values: Vec<u8> = Vec::new();
        if self.setting.num_channels() == 2 {
            values.push(Self::CALIB_DATA_LEN_2_CH as u8);
        } else if self.setting.num_channels() == 4 {
            values.push(Self::CALIB_DATA_LEN_4_CH as u8);
        } else {
            return Err(Error::InvalidChannelNumber(
                self.setting.num_channels().try_into().unwrap(),
            )
            .into());
        }
        self.set_profile(ConfigMode::Playback);
        for ch in 0..self.setting.num_channels() {
            if ch == 0 {
                values.extend(Self::TAS2563_REGISTER_ARRAY);
            }
            values.push(ch as u8);
            values.extend([0u8; 20]);
        }
        if self.setting.num_channels() == 2 {
            self.card
                .control_by_name::<TwoChannelsControl>(&self.setting.cal_data)?
                .set(values.as_slice())?;
        } else if self.setting.num_channels() == 4 {
            self.card
                .control_by_name::<FourChannelsControl>(&self.setting.cal_data)?
                .set(values.as_slice())?;
        } else {
            return Err(Error::InvalidChannelNumber(
                self.setting.num_channels().try_into().unwrap(),
            )
            .into());
        }

        // Read default values
        let data = match self.setting.rdc_ranges.len() {
            2 => {
                &(self
                    .card
                    .control_by_name::<TwoChannelsControl>(&self.setting.cal_data)?
                    .get()?)[..]
            }
            4 => {
                &(self
                    .card
                    .control_by_name::<FourChannelsControl>(&self.setting.cal_data)?
                    .get()?)[..]
            }
            _ => {
                return Err(Error::InvalidChannelNumber(
                    self.setting.num_channels().try_into().unwrap(),
                )
                .into())
            }
        };
        for ch in 0..self.setting.num_channels() {
            let start_location =
                Self::HEADER_LENGTH + Self::CHANNEL_DATA_LENGTH * ch + Self::ID_LOCATION;
            let mut value_string = String::new();
            for j in start_location..start_location + Self::CHANNEL_DATA_LENGTH {
                value_string = format!("{}{:02x}", value_string, data[j]);
            }
            vpd::write_to_vpd(&format!("dsm_calib_value_{}", ch), value_string)?;
        }

        Ok(())
    }
}

impl TAS2563 {
    const TEMP_UPPER_LIMIT_CELSIUS: f32 = 40.0;
    const TEMP_LOWER_LIMIT_CELSIUS: f32 = 0.0;
    const CALIB_DATA_LEN_2_CH: usize = 59;
    const CALIB_DATA_LEN_4_CH: usize = 101;
    const HEADER_LENGTH: usize = 17;
    const CHANNEL_DATA_LENGTH: usize = 21;
    const ID_LOCATION: usize = 0;
    const R0_LOCATION: usize = 1;
    const CALIB_APPLY_TIME: Duration = Duration::from_millis(100);
    const TAS2563_REGISTER_ARRAY: [u8; 16] = [
        0x72, 0x00, 0x0f, 0x34, 0x00, 0x0f, 0x48, 0x00, 0x0f, 0x40, 0x00, 0x0d, 0x3c, 0x00, 0x10,
        0x14,
    ];

    /// Creates an `TAS2563`.
    /// # Arguments
    ///
    /// * `card_name` - card name.
    /// * `config_path` - config file path.
    ///
    /// # Results
    ///
    /// * `TAS2563` - It implements the TAS2563 functions of boot time calibration.
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
    fn apply_calibration_value(&mut self, calib: &[TAS2563CalibData]) -> Result<()> {
        let mut values: Vec<u8> = Vec::new();
        if calib.len() == 2 {
            values.push(Self::CALIB_DATA_LEN_2_CH as u8);
        } else if calib.len() == 4 {
            values.push(Self::CALIB_DATA_LEN_4_CH as u8);
        } else {
            return Err(Error::InvalidChannelNumber(calib.len().try_into().unwrap()).into());
        }
        for (
            ch,
            &TAS2563CalibData {
                r0,
                r0_low,
                invr0,
                rms_pow,
                tlimit,
                ref register_array,
            },
        ) in calib.iter().enumerate()
        {
            if ch == 0 {
                values.extend(register_array);
            }
            values.push(ch as u8);
            values.extend(r0.to_be_bytes());
            values.extend(r0_low.to_be_bytes());
            values.extend(invr0.to_be_bytes());
            values.extend(rms_pow.to_be_bytes());
            values.extend(tlimit.to_be_bytes());
        }
        self.set_profile(ConfigMode::Calibration);
        if calib.len() == 2 {
            self.card
                .control_by_name::<TwoChannelsControl>(&self.setting.cal_data)?
                .set(values.as_slice())?;
        } else if calib.len() == 4 {
            self.card
                .control_by_name::<FourChannelsControl>(&self.setting.cal_data)?
                .set(values.as_slice())?;
        } else {
            return Err(Error::InvalidChannelNumber(calib.len().try_into().unwrap()).into());
        }

        self.set_profile(ConfigMode::Playback);
        let mut zero_player: ZeroPlayer = Default::default();
        zero_player.start(Self::CALIB_APPLY_TIME)?;
        zero_player.stop()?;
        Ok(())
    }

    /// Change the Profile and Config Id to playback mode.
    fn set_profile(&mut self, config: ConfigMode) -> Result<()> {
        // Different configs are hardcoded values.
        match config {
            ConfigMode::Playback => {
                self.card
                    .control_by_name::<IntControl>(&self.setting.speaker_profile)?
                    .set(self.setting.playback_profile_id)?;
                self.card
                    .control_by_name::<IntControl>(&self.setting.speaker_config)?
                    .set(ConfigMode::Playback as i32)?;
                self.card
                    .control_by_name::<IntControl>(&self.setting.speaker_program)?
                    .set(0)?;
            }
            ConfigMode::Calibration => {
                self.card
                    .control_by_name::<IntControl>(&self.setting.speaker_profile)?
                    .set(self.setting.calibration_profile_id)?;
                self.card
                    .control_by_name::<IntControl>(&self.setting.speaker_config)?
                    .set(ConfigMode::Calibration as i32)?;
                self.card
                    .control_by_name::<IntControl>(&self.setting.speaker_program)?
                    .set(0)?;
            }
        };

        Ok(())
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    #[test]
    fn rdc_to_ohm() {
        assert_eq!(TAS2563CalibData::rdc_to_ohm(965864218), 7.19624920189);
    }

    fn ohm_to_rdc() {
        assert_eq!(TAS2563CalibData::ohm_to_rdc(7.19624920189), 965864218);
    }

    fn pow_to_watt() {
        assert_eq!(TAS2563CalibData::pow_to_watt(4951893), 0.0368944779);
    }
    fn watt_to_pow() {
        assert_eq!(TAS2563CalibData::watt_to_pow(0.0368944779), 4951893);
    }
    fn temperature_to_cel() {
        assert_eq!(TAS2563CalibData::temperature_to_cel(734003200), 5.46875);
    }
    fn cel_to_temperature() {
        assert_eq!(TAS2563CalibData::cel_to_temperature(5.46875), 734003200);
    }
}
