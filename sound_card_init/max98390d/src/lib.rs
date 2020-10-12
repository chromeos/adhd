// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//! `max98390d` crate implements the required initialization workflows.
//! It currently supports boot time calibration for amplifiers.
#![deny(missing_docs)]
mod amp_calibration;
mod datastore;
mod error;
mod settings;
mod vpd;

use std::path::Path;

use cros_alsa::Card;
use sys_util::error;

use crate::amp_calibration::{AmpCalibration, VolumeMode};
use crate::error::{Error, Result};
use crate::settings::DeviceSettings;

/// Performs max98390d boot time calibration.
///
///
/// # Arguments
///
/// * `snd_card` - The sound card name, ex: sofcmlmax98390d.
/// * `conf` - The `DeviceSettings` in yaml format.
///
/// # Errors
///
/// If any amplifiers fail to complete the calibration.
pub fn run_max98390d(snd_card: &str, conf: &str) -> Result<()> {
    let settings = DeviceSettings::from_yaml_str(conf)?;
    let mut card = Card::new(snd_card)?;

    if !Path::new(&settings.dsm_param).exists() {
        for s in &settings.amp_calibrations {
            let mut amp_calib = match AmpCalibration::new(&mut card, s.clone()) {
                Ok(amp) => amp,
                Err(e) => {
                    error!("{}.", e);
                    continue;
                }
            };
            if let Err(e) = amp_calib.set_volume(VolumeMode::Low) {
                error!("failed to set volume to low: {}.", e);
            }
        }
        return Err(Error::MissingDSMParam);
    }
    // If some error occurs during the calibration, the iteration will continue running the
    // calibration for the next amp.
    let results: Vec<Result<()>> = settings
        .amp_calibrations
        .into_iter()
        .map(|s| {
            let mut amp_calib = AmpCalibration::new(&mut card, s)?;
            amp_calib.set_volume(VolumeMode::Low)?;
            amp_calib.run()?;
            amp_calib.set_volume(VolumeMode::High)?;
            Ok(())
        })
        .filter_map(|res| res.err())
        .map(|e| {
            error!("calibration error: {}. volume remains low.", e);
            Err(e)
        })
        .collect();

    if !results.is_empty() {
        return Err(Error::CalibrationFailed);
    }

    Ok(())
}
