// Copyright 2021 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//! `amp` crate provides `Amp` trait for amplifier initializations and `AmpBuilder`
//! to create `Amp` objects.
#![deny(missing_docs)]

mod alc1011;
mod cs35l41;
mod error;
mod max98373d;
mod max98390d;
mod tas2563;
use std::path::PathBuf;

use alc1011::ALC1011;
use cs35l41::CS35L41;
use dsm::RDCRange;
use dsm::DSM;
use log::error;
use log::info;
use max98373d::Max98373;
use max98390d::Max98390;
use serde::Serialize;
use tas2563::TAS2563;

pub use crate::error::Error;
pub use crate::error::Result;

const CONF_DIR: &str = "/etc/sound_card_init";

/// It creates `Amp` object based on the speaker amplifier name.
pub struct AmpBuilder<'a> {
    sound_card_id: &'a str,
    amp: &'a str,
    config_path: PathBuf,
}

impl<'a> AmpBuilder<'a> {
    /// Creates an `AmpBuilder`.
    /// # Arguments
    ///
    /// * `card_name` - card name.
    /// * `amp` - speaker amplifier name.
    /// * `conf_file` - config file name.
    pub fn new(sound_card_id: &'a str, amp: &'a str, conf_file: &'a str) -> Self {
        let config_path = PathBuf::from(CONF_DIR).join(conf_file);
        AmpBuilder {
            sound_card_id,
            amp,
            config_path,
        }
    }

    /// Creates an `Amp` based on the speaker amplifier name.
    pub fn build(&self) -> Result<Box<dyn Amp>> {
        match self.amp {
            "ALC1011" => {
                Ok(Box::new(ALC1011::new(self.sound_card_id, &self.config_path)?) as Box<dyn Amp>)
            }
            "CS35L41" => {
                Ok(Box::new(CS35L41::new(self.sound_card_id, &self.config_path)?) as Box<dyn Amp>)
            }
            "MAX98373" => {
                Ok(Box::new(Max98373::new(self.sound_card_id, &self.config_path)?) as Box<dyn Amp>)
            }
            "MAX98390" => {
                Ok(Box::new(Max98390::new(self.sound_card_id, &self.config_path)?) as Box<dyn Amp>)
            }
            "TAS2563" => {
                Ok(Box::new(TAS2563::new(self.sound_card_id, &self.config_path)?) as Box<dyn Amp>)
            }
            _ => Err(Error::UnsupportedAmp(self.amp.to_owned())),
        }
    }
}

/// The Amp debug information.
#[derive(Serialize, Default)]
pub struct FakeVPD {
    /// The fake dsm_calib_r0 value.
    pub r0: Vec<i32>,
    /// The fake dsm_calib_temp value.
    pub temp: Vec<i32>,
}

/// The Amp debug information.
#[derive(Serialize, Default)]
pub struct DebugInfo {
    /// The rdc acceptant range (ohm).
    pub rdc_acceptant_range: Vec<RDCRange>,
    /// The speaker rdc calibration result applied to the amp (ohm).
    pub applied_rdc: Vec<f32>,
    /// The current speaker rdc estimated by the amp (ohm).
    pub current_rdc: Option<Vec<f32>>,
    /// The current speaker rdc estimated by the amp (ohm).
    pub is_safe_mode_enabled: bool,
}

/// It defines the required functions of amplifier objects.
pub trait Amp {
    /// The amplifier boot time calibration flow.
    fn boot_time_calibration(&mut self) -> Result<()>;
    /// The amplifier RMA calibration flow.
    fn rma_calibration(&mut self) -> Result<()> {
        error!("RMA calibration is not implemented");
        Ok(())
    }
    /// Get the applied rdc value by channel index.
    fn get_applied_rdc(&mut self, ch: usize) -> Result<f32>;
    /// Get the current rdc value by channel index.
    fn get_current_rdc(&mut self, _ch: usize) -> Result<Option<f32>> {
        Ok(None)
    }
    /// Get the fake dsm_calib_r0 value by channel index.
    fn get_fake_r0(&mut self, ch: usize) -> i32;
    /// Get the fake dsm_calib_temp value by channel index.
    fn get_fake_temp(&mut self, _ch: usize) -> i32 {
        DSM::DEFAULT_FAKE_TEMP as i32
    }
    /// Set the rdc value by channel index.
    fn set_rdc(&mut self, ch: usize, rdc: f32) -> Result<()>;
    /// Set the temp value by channel index.
    fn set_temp(&mut self, ch: usize, temp: f32) -> Result<()>;

    /// Get the number of channels.
    fn num_channels(&mut self) -> usize;
    /// Get the rdc acceptant range.
    fn rdc_ranges(&mut self) -> Vec<RDCRange>;
    /// Get the current rdc value by channel index.
    fn get_debug_info(&mut self) -> Result<DebugInfo> {
        Ok(DebugInfo {
            rdc_acceptant_range: self.rdc_ranges(),
            applied_rdc: (0..self.num_channels())
                .map(|ch| self.get_applied_rdc(ch))
                .collect::<Result<Vec<f32>>>()?,
            current_rdc: (0..self.num_channels())
                .map(|ch| self.get_current_rdc(ch))
                .collect::<Result<Vec<Option<f32>>>>()?
                .into_iter()
                .collect::<Option<Vec<f32>>>(),
            is_safe_mode_enabled: self.get_safe_mode()?,
        })
    }
    /// Get an example vpd value by channel index.
    /// It is used for auto-repair job in lab testing.
    fn get_fake_vpd(&mut self) -> Result<FakeVPD> {
        Ok(FakeVPD {
            r0: (0..self.num_channels())
                .map(|ch| self.get_fake_r0(ch))
                .collect(),
            temp: (0..self.num_channels())
                .map(|ch| self.get_fake_temp(ch))
                .collect(),
        })
    }

    /// Enable or disable the amp safe mode. The amp should act like a regular amp in safe mode
    /// that it should not boost or attenuate the volume.
    fn set_safe_mode(&mut self, enable: bool) -> Result<()> {
        info!("set_safe_mode: {} is not supported", enable);
        Ok(())
    }
    /// Get the status of amp safe mode enablement.
    fn get_safe_mode(&mut self) -> Result<bool> {
        Ok(false)
    }
}
