// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
use std::fmt;
use std::io::Write;
use std::sync::atomic::{AtomicBool, Ordering};
use std::sync::{Arc, Condvar, Mutex};
use std::thread;
use std::thread::JoinHandle;
use std::time::{Duration, Instant};

use audio_streams::SampleFormat;
use cros_alsa::{Card, IntControl, SwitchControl};
use libcras::{CrasClient, CrasNodeType};
use sys_util::{error, info};

use crate::{
    datastore::Datastore,
    error::{Error, Result},
    settings::AmpCalibSettings,
    vpd::VPD,
};

const CALI_ERROR_UPPER_LIMIT: f32 = 0.3;
const CALI_ERROR_LOWER_LIMIT: f32 = 0.03;

const FRAMES_PER_BUFFER: usize = 256;
const FRAME_RATE: usize = 48000;
const NUM_CHANNELS: usize = 2;
const FORMAT: SampleFormat = SampleFormat::S16LE;
const DURATION_MS: usize = 1000;

/// Amp volume mode emulation used by set_volume().
#[derive(PartialEq)]
pub enum VolumeMode {
    /// Low mode protects the speaker by limiting its output volume if the
    /// calibration has not been completed successfully.
    Low,
    /// High mode removes the speaker output volume limitation after
    /// having successfully completed the calibration.
    High,
}

/// It implements the amplifier boot time calibration flow.
pub struct AmpCalibration<'a> {
    card: &'a mut Card,
    setting: AmpCalibSettings,
}

impl<'a> fmt::Debug for AmpCalibration<'a> {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        f.debug_struct("AmpCalibration")
            .field("snd_card_id", &self.card.name())
            .field("amp_calib", &self.setting)
            .finish()
    }
}

impl<'a> AmpCalibration<'a> {
    /// Creates an `AmpCalibration`.
    /// # Arguments
    ///
    /// * `card` - `&Card`.
    /// * `setting` - `AmpCalibSettings`.
    ///
    /// # Results
    ///
    /// * `AmpCalibration` - It implements the amplifier boot time calibration flow.
    ///
    /// # Errors
    ///
    /// * If `Card` creation from sound card name fails.
    pub fn new(card: &mut Card, setting: AmpCalibSettings) -> Result<AmpCalibration> {
        let amp = AmpCalibration { card, setting };

        Ok(amp)
    }

    /// Sets card volume control to given VolumeMode.
    pub fn set_volume(&mut self, mode: VolumeMode) -> Result<()> {
        match mode {
            VolumeMode::High => self
                .card
                .control_by_name::<IntControl>(&self.setting.amp.volume_ctrl)?
                .set(self.setting.amp.volume_high_limit)?,
            VolumeMode::Low => self
                .card
                .control_by_name::<IntControl>(&self.setting.amp.volume_ctrl)?
                .set(self.setting.amp.volume_low_limit)?,
        }
        Ok(())
    }

    /// The implementation of max98390d boot time calibration logic.
    ///
    /// The boot time calibration logic includes the following steps:
    ///  * Gets results from `do_calibration`.
    ///  * Decides whether the new calibration result should replace the stored value.
    ///  * Applies a good calibration value.
    pub fn run(&mut self) -> Result<()> {
        let vpd = VPD::from_file(&self.setting.rdc_vpd, &self.setting.temp_vpd)?;
        let (rdc_cali, temp_cali) = self.do_calibration()?;
        let datastore = match Datastore::from_file(self.card.name(), &self.setting.calib_file) {
            Ok(sci_calib) => Some(sci_calib),
            Err(e) => {
                info!("failure in Datastore::from_file: {}", e);
                None
            }
        };

        // Given that rdc_cali is the inverse of hardware real_rdc, the result of `rdc_diff`
        // equals to transforming `rdc`s to `real_rdc`s and calculating the relative difference
        // from the `real_rdc`s.
        let rdc_diff = |x: i32, x_ref: i32| (x - x_ref).abs() as f32 / x as f32;

        let diff: f32 = match datastore {
            None => rdc_diff(rdc_cali, vpd.dsm_calib_r0),
            Some(d) => match d {
                Datastore::UseVPD => rdc_diff(rdc_cali, vpd.dsm_calib_r0),
                Datastore::DSM { rdc, .. } => rdc_diff(rdc_cali, rdc),
            },
        };

        if !self.validate_temperature(temp_cali) {
            match datastore {
                None => return Err(Error::InvalidTemperature(temp_cali)),
                Some(d) => match d {
                    Datastore::UseVPD => {
                        info!("invalid temperature: {}, use VPD values.", temp_cali);
                        return Ok(());
                    }
                    Datastore::DSM { rdc, ambient_temp } => {
                        info!("invalid temperature: {}, use datastore values.", temp_cali);
                        self.card
                            .control_by_name::<IntControl>(&self.setting.amp.rdc_ctrl)?
                            .set(rdc)?;
                        self.card
                            .control_by_name::<IntControl>(&self.setting.amp.temp_ctrl)?
                            .set(ambient_temp)?;
                        return Ok(());
                    }
                },
            };
        }

        if diff > CALI_ERROR_UPPER_LIMIT {
            return Err(Error::LargeCalibrationDiff(rdc_cali, temp_cali));
        } else if diff < CALI_ERROR_LOWER_LIMIT {
            match datastore {
                None => Datastore::UseVPD.save(self.card.name(), &self.setting.calib_file)?,
                Some(d) => match d {
                    Datastore::UseVPD => {
                        info!("rdc diff: {}, use VPD values.", diff);
                    }
                    Datastore::DSM { rdc, ambient_temp } => {
                        info!("rdc diff: {}, use datastore values.", diff);
                        self.card
                            .control_by_name::<IntControl>(&self.setting.amp.rdc_ctrl)?
                            .set(rdc)?;
                        self.card
                            .control_by_name::<IntControl>(&self.setting.amp.temp_ctrl)?
                            .set(ambient_temp)?;
                    }
                },
            }
        } else {
            self.card
                .control_by_name::<IntControl>(&self.setting.amp.rdc_ctrl)?
                .set(rdc_cali)?;
            self.card
                .control_by_name::<IntControl>(&self.setting.amp.temp_ctrl)?
                .set(temp_cali)?;
            Datastore::DSM {
                rdc: rdc_cali,
                ambient_temp: temp_cali,
            }
            .save(self.card.name(), &self.setting.calib_file)?;
        }
        Ok(())
    }

    fn validate_temperature(&self, temp: i32) -> bool {
        temp < self.setting.amp.temp_upper_limit && temp > self.setting.amp.temp_lower_limit
    }

    /// Triggers the amplifier calibration and reads the calibrated rdc and ambient_temp value
    /// from the mixer control.
    /// To get accurate calibration results, the main thread calibrates the amplifier while
    /// the another thread plays zeros to the speakers.
    fn do_calibration(&mut self) -> Result<(i32, i32)> {
        // The playback worker uses `playback_started` to notify the main thread that playback
        // of zeros has started.
        let playback_started = Arc::new((Mutex::new(false), Condvar::new()));
        // Shares `calib_finished` to the playback worker and uses it to notify the worker when
        // the calibration is finished.
        let calib_finished = Arc::new(AtomicBool::new(false));
        let handle =
            AmpCalibration::run_play_zero_worker(playback_started.clone(), calib_finished.clone())?;

        // Waits until zero playback starts or timeout.
        let mut timeout = Duration::from_millis(1000);
        let (lock, cvar) = &*playback_started;
        let mut started = lock.lock()?;
        while !*started {
            let start_time = Instant::now();
            started = cvar.wait_timeout(started, timeout)?.0;
            if *started {
                break;
            } else {
                let elapsed = start_time.elapsed();
                if elapsed > timeout {
                    return Err(Error::StartPlaybackTimeout);
                } else {
                    // Spurious wakes. Decrements the sleep duration by the amount slept.
                    timeout = timeout - start_time.elapsed();
                }
            }
        }

        // Playback of zeros is started, and the main thread can start the calibration.
        self.card
            .control_by_name::<SwitchControl>(&self.setting.amp.calib_ctrl)?
            .on()?;
        let rdc = self
            .card
            .control_by_name::<IntControl>(&self.setting.amp.rdc_ctrl)?
            .get()?;
        let temp = self
            .card
            .control_by_name::<IntControl>(&self.setting.amp.temp_ctrl)?
            .get()?;
        self.card
            .control_by_name::<SwitchControl>(&self.setting.amp.calib_ctrl)?
            .off()?;
        // Notifies the play_zero_worker that the calibration is finished.
        calib_finished.store(true, Ordering::Relaxed);

        // If play_zero_worker has error during the calibration, returns an error to keep the volume
        // low to protect the speaker.
        match handle.join() {
            Ok(res) => {
                if let Err(e) = res {
                    error!("run_play_zero_worker has error: {}", e);
                    return Err(e);
                }
            }
            Err(e) => {
                error!("run_play_zero_worker panics: {:?}", e);
                return Err(Error::WorkerPanics);
            }
        }

        Ok((rdc, temp))
    }

    // Creates a thread to play zeros to the internal speakers.
    fn run_play_zero_worker(
        playback_started: Arc<(Mutex<bool>, Condvar)>,
        calib_finished: Arc<AtomicBool>,
    ) -> Result<JoinHandle<Result<()>>> {
        let mut cras_client = CrasClient::new().map_err(Error::CrasClientFailed)?;
        // TODO(b/155007305): Implement cras_client.wait_node_change and use it here.
        let node = cras_client
            .output_nodes()
            .find(|node| node.node_type == CrasNodeType::CRAS_NODE_TYPE_INTERNAL_SPEAKER)
            .ok_or(Error::InternalSpeakerNotFound)?;

        let handle = thread::spawn(move || -> Result<()> {
            let local_buffer = [0u8; FRAMES_PER_BUFFER * NUM_CHANNELS * 2];
            let iterations: usize = (FRAME_RATE * DURATION_MS) / FRAMES_PER_BUFFER / 1000;

            let (_control, mut stream) = cras_client
                .new_pinned_playback_stream(
                    node.iodev_index,
                    NUM_CHANNELS,
                    FORMAT,
                    FRAME_RATE,
                    FRAMES_PER_BUFFER,
                )
                .map_err(|e| Error::NewPlayStreamFailed(e))?;

            // Plays zeros for at most DURATION_MS.
            for i in 0..iterations {
                if calib_finished.load(Ordering::Relaxed) {
                    break;
                }
                let mut buffer = stream
                    .next_playback_buffer()
                    .map_err(|e| Error::NextPlaybackBufferFailed(e))?;
                let _write_frames = buffer.write(&local_buffer).map_err(Error::PlaybackFailed)?;

                // Notifies the main thread to start the calibration.
                if i == 1 {
                    let (lock, cvar) = &*playback_started;
                    let mut started = lock.lock()?;
                    *started = true;
                    cvar.notify_one();
                }
                // The playback_started lock is unlocked here when `started` goes out of scope.
            }

            // Returns an error if the calibration is not finished before playback stops.
            if !calib_finished.load(Ordering::Relaxed) {
                return Err(Error::CalibrationTimeout);
            }
            Ok(())
        });

        Ok(handle)
    }
}
