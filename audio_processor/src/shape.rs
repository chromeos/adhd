// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use serde::Deserialize;
use serde::Serialize;

/// Shape of a audio buffer.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct Shape {
    /// Number of channels.
    pub channels: usize,
    /// Number of frames.
    pub frames: usize,
}

/// Format of an audio processor.
#[derive(Debug, Clone, Copy, PartialEq, Eq, Serialize, Deserialize)]
pub struct Format {
    /// Number of channels.
    pub channels: usize,
    /// Number of frames per processing.
    pub block_size: usize,
    /// Number of frames per second.
    pub frame_rate: usize,
}

impl Into<Shape> for Format {
    fn into(self) -> Shape {
        Shape {
            channels: self.channels,
            frames: self.block_size,
        }
    }
}
