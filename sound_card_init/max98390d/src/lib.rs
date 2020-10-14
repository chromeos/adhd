// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//! `max98390d` crate implements the required initialization workflows.
//! It currently supports boot time calibration for max98390d.
#![deny(missing_docs)]
mod amp_calibration;
mod datastore;
mod error;
mod settings;
mod vpd;

use std::fs;
use std::path::{Path, PathBuf};
use std::time::{Duration, SystemTime, UNIX_EPOCH};

use cros_alsa::Card;
use sys_util::error;
use utils::{run_time, shutdown_time, DATASTORE_DIR};

use crate::amp_calibration::{AmpCalibration, VolumeMode};
use crate::error::{Error, Result};
use crate::settings::DeviceSettings;

const SPEAKER_COOL_DOWN_TIME: Duration = Duration::from_secs(180);

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
        set_all_volume_low(&mut card, &settings);
        return Err(Error::MissingDSMParam);
    }

    // Needs to check whether the speakers are over heated if it is not the first time boot.
    if run_time::exists(snd_card) {
        if let Err(err) = check_speaker_over_heated(snd_card, SPEAKER_COOL_DOWN_TIME) {
            match err {
                Error::HotSpeaker => run_all_hot_speaker_workflow(&mut card, &settings),
                _ => {
                    // We cannot assume the speakers are not replaced or not over heated
                    // when the shutdown time file is invalid; therefore we can not use the datastore
                    // value anymore and we can not trigger boot time calibration.
                    del_all_datastore(snd_card, &settings);
                    set_all_volume_low(&mut card, &settings);
                }
            };
            return Err(err);
        };
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

fn del_all_datastore(snd_card: &str, settings: &DeviceSettings) {
    for s in &settings.amp_calibrations {
        if let Err(e) = fs::remove_file(
            PathBuf::from(DATASTORE_DIR)
                .join(snd_card)
                .join(&s.calib_file),
        ) {
            error!("failed to remove datastore: {}.", e);
        }
    }
}

fn run_all_hot_speaker_workflow(card: &mut Card, settings: &DeviceSettings) {
    for s in &settings.amp_calibrations {
        let mut amp_calib = match AmpCalibration::new(card, s.clone()) {
            Ok(amp) => amp,
            Err(e) => {
                error!("{}.", e);
                continue;
            }
        };
        if let Err(e) = amp_calib.hot_speaker_workflow() {
            error!("failed to run hot_speaker_workflow: {}.", e);
        }
    }
}

fn set_all_volume_low(card: &mut Card, settings: &DeviceSettings) {
    for s in &settings.amp_calibrations {
        let mut amp_calib = match AmpCalibration::new(card, s.clone()) {
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
}

// If (Current time - the latest CRAS shutdown time) < cool_down_time, we assume that
// the speakers may be over heated.
fn check_speaker_over_heated(snd_card: &str, cool_down_time: Duration) -> Result<()> {
    let last_run = run_time::from_file(snd_card).map_err(Error::ReadTimestampFailed)?;
    let last_shutdown = shutdown_time::from_file().map_err(Error::ReadTimestampFailed)?;
    if last_shutdown < last_run {
        return Err(Error::InvalidShutDownTime);
    }

    let now = SystemTime::now()
        .duration_since(UNIX_EPOCH)
        .map_err(Error::SystemTimeError)?;

    let elapsed = now
        .checked_sub(last_shutdown)
        .ok_or(Error::InvalidShutDownTime)?;

    if elapsed < cool_down_time {
        return Err(Error::HotSpeaker);
    }
    Ok(())
}
