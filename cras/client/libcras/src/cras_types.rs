// Copyright 2019 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use cras_sys::gen::CRAS_STREAM_DIRECTION;

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
