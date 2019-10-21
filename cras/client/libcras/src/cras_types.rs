// Copyright 2019 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use audio_streams::SampleFormat;
use cras_sys::gen::{snd_pcm_format_t, CRAS_STREAM_DIRECTION};

/// An enum of the valid directions of an audio stream.
/// Convertible into CRAS_STREAM_DIRECTION via direction.into()
#[derive(Copy, Clone, Debug, PartialEq)]
pub enum StreamDirection {
    Playback,
    Capture,
}

impl Into<CRAS_STREAM_DIRECTION> for StreamDirection {
    fn into(self) -> CRAS_STREAM_DIRECTION {
        match self {
            StreamDirection::Playback => CRAS_STREAM_DIRECTION::CRAS_STREAM_OUTPUT,
            StreamDirection::Capture => CRAS_STREAM_DIRECTION::CRAS_STREAM_INPUT,
        }
    }
}

/// Convert an audio_streams SampleFormat into the corresponding pcm_format.
pub fn pcm_format(format: SampleFormat) -> snd_pcm_format_t {
    match format {
        SampleFormat::U8 => snd_pcm_format_t::SND_PCM_FORMAT_U8,
        SampleFormat::S16LE => snd_pcm_format_t::SND_PCM_FORMAT_S16_LE,
        SampleFormat::S24LE => snd_pcm_format_t::SND_PCM_FORMAT_S24_LE,
        SampleFormat::S32LE => snd_pcm_format_t::SND_PCM_FORMAT_S32_LE,
    }
}
