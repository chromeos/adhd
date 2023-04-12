// Copyright 2020 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//! `max98390d` module implements the required initialization workflows for sound
//! cards that use max98390d smart amp.
//! It currently supports boot time calibration for max98390d.
#![deny(missing_docs)]
mod error;
mod settings;

use std::fmt;
use std::fmt::Debug;
use std::fmt::Formatter;
use std::fs;
use std::path::Path;
use std::time::Duration;

use cros_alsa::Card;
use cros_alsa::IntControl;
use cros_alsa::SwitchControl;
use dsm::CalibData;
use dsm::RDCRange;
use dsm::SpeakerStatus;
use dsm::TempConverter;
use dsm::ZeroPlayer;
use dsm::DSM;
pub use error::Error;
use log::info;
use settings::AmpCalibSettings;
use settings::DeviceSettings;

use crate::Amp;
use crate::Result;

/// Amp volume mode emulation used by set_volume().
#[derive(PartialEq, Clone, Copy)]
enum VolumeMode {
    /// Low mode protects the speaker by limiting its output volume if the
    /// calibration has not been completed successfully.
    Low = 138,
    /// High mode removes the speaker output volume limitation after
    /// having successfully completed the calibration.
    High = 148,
}

/// It implements the Max98390 functions of boot time calibration.
#[derive(Debug)]
pub struct Max98390 {
    card: Card,
    setting: AmpCalibSettings,
}

/// `Max98390CalibData` represents the Max98390 calibration data.
#[derive(Clone, Copy)]
struct Max98390CalibData {
    /// The calibrated raw value of DC resistance of the speaker.
    pub rdc: i32,
    /// The ambient temperature in celsius unit at which the rdc is measured.
    pub temp: f32,
}

impl CalibData for Max98390CalibData {
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
        3.66 * (1 << 20) as f32 / x as f32
    }
}

impl Max98390CalibData {
    /// Converts the ambient temperature from celsius to the DSM unit.
    #[inline]
    fn celsius_to_dsm_unit(celsius: f32) -> i32 {
        (celsius * ((1 << 12) as f32) / 100.0) as i32
    }

    /// Converts the ambient temperature from  DSM unit to celsius.
    #[inline]
    fn dsm_unit_to_celsius(temp: i32) -> f32 {
        temp as f32 * 100.0 / (1 << 12) as f32
    }

    /// Converts the adaptive_rdc control value to real DC resistance in ohm unit.
    #[inline]
    fn adaptive_rdc_to_ohm(x: i32) -> f32 {
        x as f32 / 256.0 * 3.66 as f32
    }
}

impl Debug for Max98390CalibData {
    fn fmt(&self, f: &mut Formatter) -> fmt::Result {
        self.debug_fmt(f)
    }
}

impl Amp for Max98390 {
    /// Performs max98390d boot time calibration.
    ///
    /// # Errors
    ///
    /// If the amplifier fails to complete the calibration.
    fn boot_time_calibration(&mut self) -> Result<()> {
        for setting in &self.setting.controls {
            if !Path::new(&setting.dsm_param).exists() {
                return Err(Error::MissingDSMParam.into());
            }
        }

        let mut dsm = DSM::new(
            self.card.name(),
            self.setting.num_channels(),
            Self::TEMP_UPPER_LIMIT_CELSIUS,
            Self::TEMP_LOWER_LIMIT_CELSIUS,
        );
        dsm.set_temp_converter(TempConverter::new(
            Max98390CalibData::dsm_unit_to_celsius,
            Max98390CalibData::celsius_to_dsm_unit,
        ));

        // Needs Rdc updates to be done after internal speaker is ready otherwise
        // it would be overwritten by the DSM blob update.
        dsm.wait_for_speakers_ready()?;

        let volume_state = self.get_volume_state()?;

        self.set_volume(VolumeMode::Low)?;
        let calib = if !self.setting.boot_time_calibration_enabled {
            info!("skip boot time calibration and use vpd values");
            dsm.get_all_vpd_calibration_value()?
        } else {
            match dsm.check_speaker_over_heated_workflow()? {
                SpeakerStatus::Hot(previous_calib) => previous_calib,
                SpeakerStatus::Cold => self
                    .do_calibration()?
                    .iter()
                    .enumerate()
                    .map(|(ch, calib_data)| {
                        dsm.decide_calibration_value_workflow(ch, *calib_data)
                            .map_err(crate::Error::DSMError)
                    })
                    .collect::<Result<Vec<_>>>()?,
            }
        };
        info!("applied {:?}", calib);
        self.apply_calibration_value(calib)?;
        info!("applied {:?}", volume_state);
        self.set_volume_from_state(volume_state)?;
        Ok(())
    }

    fn get_applied_rdc(&mut self, ch: usize) -> Result<f32> {
        if ch >= self.setting.controls.len() {
            return Err(dsm::Error::InvalidChannelNumer(ch).into());
        }

        Ok(Max98390CalibData::rdc_to_ohm(
            self.card
                .control_by_name::<IntControl>(&self.setting.controls[ch].rdc_ctrl)?
                .get()?,
        ))
    }

    fn get_current_rdc(&mut self, ch: usize) -> Result<Option<f32>> {
        if ch >= self.setting.controls.len() {
            return Err(dsm::Error::InvalidChannelNumer(ch).into());
        }

        let mut zero_player: ZeroPlayer = Default::default();
        zero_player.start(Self::RDC_CALIB_WARM_UP_TIME)?;
        let rdc = Max98390CalibData::adaptive_rdc_to_ohm(
            self.card
                .control_by_name::<IntControl>(&self.setting.controls[ch].adaptive_rdc_ctrl)?
                .get()?,
        );
        zero_player.stop()?;
        Ok(Some(rdc))
    }

    fn num_channels(&mut self) -> usize {
        self.setting.num_channels()
    }

    fn rdc_ranges(&mut self) -> Vec<RDCRange> {
        self.setting.rdc_ranges.clone()
    }
}

impl Max98390 {
    const TEMP_UPPER_LIMIT_CELSIUS: f32 = 40.0;
    const TEMP_LOWER_LIMIT_CELSIUS: f32 = 0.0;
    const RDC_CALIB_WARM_UP_TIME: Duration = Duration::from_millis(300);

    /// Creates an `Max98390`.
    /// # Arguments
    ///
    /// * `card_name` - card name.
    /// * `config_path` - config file path.
    ///
    /// # Results
    ///
    /// * `Max98390` - It implements the Max98390 functions of boot time calibration.
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

    /// Get the current volume for each card control listed in Amp Calib Settings.
    /// # Reuslts
    ///
    /// * `Vec(String, i32)` - An array that contains the name of the control and its associated
    ///                        volume.
    fn get_volume_state(&mut self) -> Result<Vec<(String, i32)>> {
        let mut volume_state: Vec<(String, i32)> = vec![];

        for control in &self.setting.controls {
            let volume = self
                .card
                .control_by_name::<IntControl>(&control.volume_ctrl)?
                .get()?;

            volume_state.push((control.volume_ctrl.clone(), volume));
        }
        Ok(volume_state)
    }

    /// Set the volume of the card control listed in `state`.
    /// # Arguments
    ///
    /// * `state` - a vector with tuples of desired volume associated with the name of the control,
    ///             format: [(control_anme, volume)].
    ///
    fn set_volume_from_state(&mut self, state: Vec<(String, i32)>) -> Result<()> {
        for (control_name, volume) in state {
            self.card
                .control_by_name::<IntControl>(&control_name)?
                .set(volume)?;
        }
        Ok(())
    }

    /// Sets the card volume control to given VolumeMode.
    fn set_volume(&mut self, mode: VolumeMode) -> Result<()> {
        for control in &self.setting.controls {
            self.card
                .control_by_name::<IntControl>(&control.volume_ctrl)?
                .set(mode as i32)?;
        }
        Ok(())
    }

    /// Applies the calibration value to the amp.
    fn apply_calibration_value(&mut self, calib: Vec<Max98390CalibData>) -> Result<()> {
        for (ch, &Max98390CalibData { rdc, temp }) in calib.iter().enumerate() {
            self.card
                .control_by_name::<IntControl>(&self.setting.controls[ch].rdc_ctrl)?
                .set(rdc)?;
            self.card
                .control_by_name::<IntControl>(&self.setting.controls[ch].temp_ctrl)?
                .set(Max98390CalibData::celsius_to_dsm_unit(temp))?;
        }
        Ok(())
    }

    /// Triggers the amplifier calibration and reads the calibrated rdc and ambient_temp value
    /// from the mixer control.
    /// To get accurate calibration results, the main thread calibrates the amplifier while
    /// the `zero_player` starts another thread to play zeros to the speakers.
    fn do_calibration(&mut self) -> Result<Vec<Max98390CalibData>> {
        let mut zero_player: ZeroPlayer = Default::default();
        zero_player.start(Self::RDC_CALIB_WARM_UP_TIME)?;
        // Playback of zeros is started for Self::RDC_CALIB_WARM_UP_TIME, and the main thread
        // can start the calibration.
        let setting = &self.setting;
        let card = &mut self.card;
        let calib = setting
            .controls
            .iter()
            .map(|control| {
                card.control_by_name::<SwitchControl>(&control.calib_ctrl)?
                    .on()?;
                let rdc = card
                    .control_by_name::<IntControl>(&control.rdc_ctrl)?
                    .get()?;
                let temp = card
                    .control_by_name::<IntControl>(&control.temp_ctrl)?
                    .get()?;
                card.control_by_name::<SwitchControl>(&control.calib_ctrl)?
                    .off()?;
                Ok(Max98390CalibData {
                    rdc,
                    temp: Max98390CalibData::dsm_unit_to_celsius(temp),
                })
            })
            .collect::<Result<Vec<Max98390CalibData>>>()?;
        zero_player.stop()?;
        info!("Boot tiime calibration results: {:?}", calib);
        Ok(calib)
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    #[test]
    fn celsius_to_dsm_unit() {
        assert_eq!(
            Max98390CalibData::celsius_to_dsm_unit(Max98390::TEMP_UPPER_LIMIT_CELSIUS),
            1638
        );
        assert_eq!(
            Max98390CalibData::celsius_to_dsm_unit(Max98390::TEMP_LOWER_LIMIT_CELSIUS),
            0
        );
    }

    #[test]
    fn dsm_unit_to_celsius() {
        assert_eq!(
            Max98390CalibData::dsm_unit_to_celsius(1638).round(),
            Max98390::TEMP_UPPER_LIMIT_CELSIUS
        );
        assert_eq!(
            Max98390CalibData::dsm_unit_to_celsius(0),
            Max98390::TEMP_LOWER_LIMIT_CELSIUS
        );
    }

    #[test]
    fn rdc_to_ohm() {
        assert_eq!(Max98390CalibData::rdc_to_ohm(1123160), 3.416956);
        assert_eq!(Max98390CalibData::rdc_to_ohm(1157049), 3.3168762);
    }

    #[test]
    fn adaptive_rdc_to_ohm() {
        assert_eq!(Max98390CalibData::adaptive_rdc_to_ohm(245), 3.5027344);
        assert_eq!(Max98390CalibData::adaptive_rdc_to_ohm(551), 7.8775783);
    }
}
