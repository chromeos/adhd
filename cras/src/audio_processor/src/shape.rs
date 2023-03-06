// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

/// Shape of a audio buffer.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct Shape {
    /// Number of channels.
    pub channels: usize,
    /// Number of frames.
    pub frames: usize,
}
