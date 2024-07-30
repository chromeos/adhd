// Copyright 2020 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//! `dsm` crate implements the required initialization workflows for smart amps.

mod datastore;
mod error;
pub mod metrics;
pub mod utils;
pub mod vpd;
mod zero_player;

use std::fmt;
use std::thread;
use std::time::Duration;
use std::time::SystemTime;
use std::time::UNIX_EPOCH;

use libcras::CrasClient;
use libcras::CrasNodeType;
use log::error;
use log::info;
use serde::Deserialize;
use serde::Serialize;

use crate::datastore::Datastore;
pub use crate::error::Error;
pub use crate::error::Result;
use crate::metrics::*;
use crate::utils::run_time;
use crate::utils::shutdown_time;
pub use crate::zero_player::ZeroPlayer;

#[derive(Debug, Default, PartialEq, Serialize, Deserialize, Clone, Copy)]
pub struct RDCRange {
    pub lower: f32,
    pub upper: f32,
}

/// `CalibData` is the trait for the calibration data.
pub trait CalibData: Send + fmt::Debug + From<Self::VPDType> {
    type VPDType: VPDTrait;
    /// The function to convert the rdc to ohm unit.
    fn rdc_to_ohm(rdc: i32) -> f32
    where
        Self: Sized;
    /// The function to convert rdc from ohm unit to raw data.
    fn ohm_to_rdc(rdc: f32) -> i32
    where
        Self: Sized;
    /// The function to return the rdc in raw data.
    fn rdc(&self) -> i32;
    /// The function to return the rdc in ohm unit.
    fn rdc_ohm(&self) -> f32
    where
        Self: Sized,
    {
        Self::rdc_to_ohm(self.rdc())
    }
    /// Return the ambient temperature in celsius unit at which the rdc is
    /// measured.
    fn temp(&self) -> f32;

    /// It is used in the trait implementer's fmt::Debug so that the debug message
    /// can be the same for all amps.
    fn debug_fmt(&self, f: &mut fmt::Formatter) -> fmt::Result
    where
        Self: Sized,
    {
        write!(
            f,
            "( rdc: {} ohm, temperature: {} C )",
            self.rdc_ohm(),
            self.temp()
        )
    }
}

// `VPDTrait` defines that any VPD can be created by giving the channel number.
pub trait VPDTrait {
    fn new(channel: usize) -> Result<Self>
    where
        Self: Sized;
    fn new_from_datastore(datastore: Datastore, channel: usize) -> Result<Self>
    where
        Self: Sized;
}

/// `TempConverter` converts the temperature value between celsius and unit in VPD::dsm_calib_temp.
pub struct TempConverter {
    vpd_to_celsius: fn(i32) -> f32,
    celsius_to_vpd: fn(f32) -> i32,
}

impl Default for TempConverter {
    fn default() -> Self {
        let vpd_to_celsius = |x: i32| x as f32;
        let celsius_to_vpd = |x: f32| x.round() as i32;
        Self {
            vpd_to_celsius,
            celsius_to_vpd,
        }
    }
}

impl TempConverter {
    /// Creates a `TempConverter`
    ///
    /// # Arguments
    ///
    /// * `vpd_to_celsius` - function to convert VPD::dsm_calib_temp to celsius unit`
    /// * `celsius_to_vpd` - function to convert celsius unit to VPD::dsm_calib_temp`
    /// # Results
    ///
    /// * `TempConverter` - it converts the temperature value between celsius and unit in VPD::dsm_calib_temp.
    pub fn new(vpd_to_celsius: fn(i32) -> f32, celsius_to_vpd: fn(f32) -> i32) -> Self {
        Self {
            vpd_to_celsius,
            celsius_to_vpd,
        }
    }
}

/// `SpeakerStatus` are the possible return results of
/// DSM::check_speaker_over_heated_workflow.
pub enum SpeakerStatus<T: CalibData> {
    ///`SpeakerStatus::Cold` means the speakers are not overheated and the Amp can
    /// trigger the boot time calibration.
    Cold,
    /// `SpeakerStatus::Hot(Vec<CalibData>)` means the speakers may be too hot for calibration.
    /// The boot time calibration should be skipped and the Amp should use the previous
    /// calibration values returned by the enum.
    Hot(Vec<T>),
}

/// `DSM`, which implements the required initialization workflows for smart amps.
pub struct DSM {
    snd_card: String,
    num_channels: usize,
    temp_converter: TempConverter,
    temp_upper_limit: f32,
    temp_lower_limit: f32,
}

impl DSM {
    pub const DEFAULT_FAKE_TEMP: f32 = 28.0;
    const SPEAKER_COOL_DOWN_TIME: Duration = Duration::from_secs(180);
    const CALI_ERROR_LOWER_LIMIT: f32 = 0.03;

    /// Creates a `DSM`
    ///
    /// # Arguments
    ///
    /// * `snd_card` - `sound card name`.
    /// * `num_channels` - `number of channels`.
    /// * `temp_upper_limit` - the high limit of the valid ambient temperature in dsm unit.
    /// * `temp_lower_limit` - the low limit of the valid ambient temperature in dsm unit.
    ///
    /// # Results
    ///
    /// * `DSM` - It implements the required initialization workflows for smart amps.
    pub fn new(
        snd_card: &str,
        num_channels: usize,
        temp_upper_limit: f32,
        temp_lower_limit: f32,
    ) -> Self {
        Self {
            snd_card: snd_card.to_owned(),
            num_channels,
            temp_converter: TempConverter::default(),
            temp_upper_limit,
            temp_lower_limit,
        }
    }

    /// Sets self.temp_converter to the given temp_converter.
    ///
    /// # Arguments
    ///
    /// * `temp_converter` - the convert function to use.
    pub fn set_temp_converter(&mut self, temp_converter: TempConverter) {
        self.temp_converter = temp_converter;
    }

    /// Checks whether the speakers are overheated or not according to the previous shutdown time.
    /// The boot time calibration should be skipped when the speakers may be too hot
    /// and the Amp should use the previous calibration value returned by the
    /// SpeakerStatus::Hot(Vec<CalibData>).
    ///
    /// # Results
    ///
    /// * `SpeakerStatus::Cold` - which means the speakers are not overheated and the Amp can
    ///    trigger the boot time calibration.
    /// * `SpeakerStatus::Hot(Vec<CalibData>)` - when the speakers may be too hot. The boot
    ///   time calibration should be skipped and the Amp should use the previous calibration values
    ///   returned by the enum.
    ///
    /// # Errors
    ///
    /// * The speakers are overheated and there are no previous calibration values stored.
    /// * Cannot determine whether the speakers are overheated as previous shutdown time record is
    ///   invalid.
    pub fn check_speaker_over_heated_workflow<T: CalibData>(&self) -> Result<SpeakerStatus<T>> {
        if self.is_first_boot() {
            return Ok(SpeakerStatus::Cold);
        }
        match self.is_speaker_over_heated() {
            Ok(overheated) => {
                if overheated {
                    let calib = self.get_all_previous_calibration_value()?;
                    info!("the speakers are hot, the boot time calibration should be skipped");
                    return Ok(SpeakerStatus::Hot(calib));
                }
                Ok(SpeakerStatus::Cold)
            }
            Err(e) => {
                // when the shutdown time file is invalid we assume the speakers are overheated
                // and we can not trigger boot time calibration.
                info!("{}, the boot time calibration is skipped", e);
                let calib = self.get_all_previous_calibration_value()?;
                return Ok(SpeakerStatus::Hot(calib));
            }
        }
    }

    /// Decides a good calibration value and updates the stored value according to the following
    /// logic:
    /// * Returns the previous value if the ambient temperature is not within a valid range.
    ///   If previous value does not exist, use VPD value instead.
    /// * Returns the previous value if the rdc difference is smaller than `CALI_ERROR_LOWER_LIMIT`.
    /// * Returns the boot time calibration value and updates the datastore value if the rdc
    ///   difference is within the `rdc_range`and larger than the `CALI_ERROR_LOWER_LIMIT`.
    ///
    /// # Arguments
    ///
    /// * `card` - `&Card`.
    /// * `channel` - `channel number`.
    /// * `calib_data` - `boot time calibrated data`.
    /// * `rdc_range` - rdc_acceptant_range in the speaker data sheet.
    ///
    /// # Results
    ///
    /// * `CalibData` - the calibration data to be applied according to the deciding logic.
    ///
    /// # Errors
    ///
    /// * VPD does not exist.
    /// * If rdc is not within the `rdc_range`.
    /// * Failed to update Datastore.
    pub fn decide_calibration_value_workflow<T: CalibData + 'static>(
        &self,
        channel: usize,
        calib_data: T,
        rdc_range: RDCRange,
    ) -> Result<T> {
        // Look for datastore first
        let (datastore_exist, previous_calib) = match self.get_previous_calibration_value(channel) {
            Ok(previous_calib) => (true, previous_calib),
            Err(e) => {
                info!("{}, use vpd as previous calibration value", e);
                (false, self.get_vpd_calibration_value(channel)?)
            }
        };
        info!(
            "boot_time_calib: {:?}, previous_calib: {:?}",
            calib_data, previous_calib
        );

        if calib_data.temp() < self.temp_lower_limit || calib_data.temp() > self.temp_upper_limit {
            info!(
                "invalid temperature: {}. use previous calibration value",
                calib_data.temp()
            );

            if !datastore_exist {
                Datastore::UseVPD.save(&self.snd_card, channel)?;
            }
            log_uma_enum(UMACalibrationResult::UsePreviousValue);
            return Ok(previous_calib);
        }

        if calib_data.rdc_ohm() <= rdc_range.lower || calib_data.rdc_ohm() >= rdc_range.upper {
            info!(
                "invalid rdc: {}, rdc_acceptant_range: [{},{}], use previous calibration value",
                calib_data.rdc_ohm(),
                rdc_range.lower,
                rdc_range.upper,
            );
            log_uma_enum(UMACalibrationResult::LargeCalibrationDiff);
            return Ok(previous_calib);
        }

        let diff = {
            let calib_rdc_ohm = calib_data.rdc_ohm();
            let previous_rdc_ohm = previous_calib.rdc_ohm();
            (calib_rdc_ohm - previous_rdc_ohm) / previous_rdc_ohm
        };
        if diff < Self::CALI_ERROR_LOWER_LIMIT {
            if !datastore_exist {
                Datastore::UseVPD.save(&self.snd_card, channel)?;
            }
            log_uma_enum(UMACalibrationResult::UsePreviousValue);
            Ok(previous_calib)
        } else {
            Datastore::DSM {
                rdc: calib_data.rdc(),
                temp: (self.temp_converter.celsius_to_vpd)(calib_data.temp()),
            }
            .save(&self.snd_card, channel)?;
            log_uma_enum(UMACalibrationResult::NewCalibrationValue);
            Ok(calib_data)
        }
    }

    /// Gets the calibration values from vpd.
    ///
    /// # Results
    ///
    /// * `Vec<CalibData>` - the calibration values in vpd.
    ///
    /// # Errors
    ///
    /// * Failed to read vpd.
    pub fn get_all_vpd_calibration_value<T: CalibData>(&self) -> Result<Vec<T>> {
        (0..self.num_channels)
            .map(|ch| self.get_vpd_calibration_value(ch))
            .collect::<Result<Vec<_>>>()
    }

    /// Gets the previous calibration values from datastore
    ///
    /// # Results
    ///
    /// * `Vec<CalibData>` - the calibration values in datastore.
    ///
    /// # Errors
    ///
    /// * Failed to read datastore.
    pub fn get_all_previous_calibration_value<T: CalibData>(&self) -> Result<Vec<T>> {
        (0..self.num_channels)
            .map(|ch| self.get_previous_calibration_value(ch))
            .collect::<Result<Vec<_>>>()
    }

    /// Blocks until the internal speakers are ready.
    ///
    /// # Errors
    ///
    /// * Failed to wait the internal speakers to be ready.
    pub fn wait_for_speakers_ready(&self) -> Result<()> {
        let find_speaker = || -> Result<()> {
            let cras_client = CrasClient::new()?;
            let _node = cras_client
                .output_nodes()
                .find(|node| node.node_type == CrasNodeType::CRAS_NODE_TYPE_INTERNAL_SPEAKER)
                .ok_or(Error::InternalSpeakerNotFound)?;
            Ok(())
        };
        // TODO(b/155007305): Implement cras_client.wait_node_change and use it here.
        const RETRY: usize = 10;
        const RETRY_INTERVAL: Duration = Duration::from_millis(500);
        for _ in 0..RETRY {
            match find_speaker() {
                Ok(_) => {
                    log_uma_enum(UMAWaitForSpeaker::OK);
                    return Ok(());
                }
                Err(e) => error!("retry on finding speaker: {}", e),
            };
            thread::sleep(RETRY_INTERVAL);
        }
        log_uma_enum(UMAWaitForSpeaker::Error);
        Err(Error::InternalSpeakerNotFound)
    }

    fn is_first_boot(&self) -> bool {
        !run_time::exists(&self.snd_card)
    }

    // If (Current time - the latest CRAS shutdown time) < cool_down_time, we assume that
    // the speakers may be overheated.
    fn is_speaker_over_heated(&self) -> Result<bool> {
        let last_run = run_time::from_file(&self.snd_card)?;
        let last_shutdown = shutdown_time::from_file()?;
        if last_shutdown < last_run {
            return Err(Error::InvalidShutDownTime);
        }

        let now = SystemTime::now().duration_since(UNIX_EPOCH)?;

        let elapsed = now
            .checked_sub(last_shutdown)
            .ok_or(Error::InvalidShutDownTime)?;

        if elapsed < Self::SPEAKER_COOL_DOWN_TIME {
            return Ok(true);
        }
        Ok(false)
    }

    fn get_previous_calibration_value<T: CalibData>(&self, ch: usize) -> Result<T> {
        let sci_calib = Datastore::from_file(&self.snd_card, ch)?;
        Ok(T::from(T::VPDType::new_from_datastore(sci_calib, ch)?))
    }

    fn get_vpd_calibration_value<T: CalibData>(&self, channel: usize) -> Result<T> {
        Ok(T::from(T::VPDType::new(channel)?))
    }
}
