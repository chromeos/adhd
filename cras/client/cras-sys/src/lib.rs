// Copyright 2019 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
extern crate data_model;

use std::os::raw::c_char;

#[allow(dead_code)]
#[allow(non_upper_case_globals)]
#[allow(non_camel_case_types)]
#[allow(non_snake_case)]
pub mod gen;
use gen::{
    _snd_pcm_format, audio_message, cras_audio_format_packed, cras_iodev_info, cras_ionode_info,
    cras_ionode_info__bindgen_ty_1, cras_timespec, CRAS_AUDIO_MESSAGE_ID, CRAS_CHANNEL,
};

unsafe impl data_model::DataInit for gen::audio_message {}
unsafe impl data_model::DataInit for gen::cras_client_connected {}
unsafe impl data_model::DataInit for gen::cras_client_stream_connected {}
unsafe impl data_model::DataInit for gen::cras_connect_message {}
unsafe impl data_model::DataInit for gen::cras_disconnect_stream_message {}
unsafe impl data_model::DataInit for gen::cras_iodev_info {}
unsafe impl data_model::DataInit for gen::cras_ionode_info {}
unsafe impl data_model::DataInit for gen::cras_server_state {}
unsafe impl data_model::DataInit for gen::cras_set_system_mute {}
unsafe impl data_model::DataInit for gen::cras_set_system_volume {}

impl cras_audio_format_packed {
    /// Initializes `cras_audio_format_packed` from input parameters.
    ///
    /// # Arguments
    /// * `format` - Format in used.
    /// * `rate` - Rate in used.
    /// * `num_channels` - Number of channels in used.
    ///
    /// # Returns
    /// Structure `cras_audio_format_packed`
    pub fn new(format: _snd_pcm_format, rate: usize, num_channels: usize) -> Self {
        let mut audio_format = Self {
            format: format as i32,
            frame_rate: rate as u32,
            num_channels: num_channels as u32,
            channel_layout: [-1; CRAS_CHANNEL::CRAS_CH_MAX as usize],
        };
        for i in 0..CRAS_CHANNEL::CRAS_CH_MAX as usize {
            if i < num_channels {
                audio_format.channel_layout[i] = i as i8;
            } else {
                break;
            }
        }
        audio_format
    }
}

impl Default for audio_message {
    fn default() -> Self {
        Self {
            error: 0,
            frames: 0,
            id: CRAS_AUDIO_MESSAGE_ID::NUM_AUDIO_MESSAGES,
        }
    }
}

impl Default for cras_iodev_info {
    fn default() -> Self {
        Self {
            idx: 0,
            name: [0; 64usize],
            stable_id: 0,
        }
    }
}

#[derive(Debug)]
pub struct CrasIodevInfo {
    pub index: u32,
    pub name: String,
}

fn cstring_to_string(cstring: &[c_char]) -> String {
    let null_idx = match cstring.iter().enumerate().find(|(_, &c)| c == 0) {
        Some((i, _)) => i,
        None => return "".to_owned(),
    };

    let ptr = cstring.as_ptr() as *const u8;
    let slice = unsafe { core::slice::from_raw_parts(ptr, null_idx) };
    String::from_utf8_lossy(slice).to_string()
}

impl From<cras_iodev_info> for CrasIodevInfo {
    fn from(info: cras_iodev_info) -> Self {
        Self {
            index: info.idx,
            name: cstring_to_string(&info.name),
        }
    }
}

impl Default for cras_ionode_info {
    fn default() -> Self {
        Self {
            iodev_idx: 0,
            ionode_idx: 0,
            plugged: 0,
            active: 0,
            plugged_time: cras_ionode_info__bindgen_ty_1 {
                tv_sec: 0,
                tv_usec: 0,
            },
            volume: 0,
            capture_gain: 0,
            left_right_swapped: 0,
            type_enum: 0,
            stable_id: 0,
            mic_positions: [0; 128usize],
            type_: [0; 32usize],
            name: [0; 64usize],
            active_hotword_model: [0; 16usize],
        }
    }
}

#[derive(Debug)]
pub struct CrasIonodeInfo {
    pub name: String,
    pub iodev_index: u32,
    pub ionode_index: u32,
    pub stable_id: u32,
    pub plugged: bool,
    pub active: bool,
    pub type_name: String,
    pub volume: u32,
    pub capture_gain: i32,
    pub plugged_time: cras_timespec,
}

impl From<cras_ionode_info> for CrasIonodeInfo {
    fn from(info: cras_ionode_info) -> Self {
        Self {
            name: cstring_to_string(&info.name),
            iodev_index: info.iodev_idx,
            ionode_index: info.ionode_idx,
            stable_id: info.stable_id,
            plugged: info.plugged != 0,
            active: info.active != 0,
            type_name: cstring_to_string(&info.type_),
            volume: info.volume,
            capture_gain: info.capture_gain,
            plugged_time: cras_timespec {
                tv_sec: info.plugged_time.tv_sec,
                tv_nsec: info.plugged_time.tv_usec * 1000,
            },
        }
    }
}
