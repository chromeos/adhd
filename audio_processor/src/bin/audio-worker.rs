// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use std::os::fd::FromRawFd;
use std::os::fd::OwnedFd;

/// The audio-worker accepts commands coming from fd 3.
/// It is a helper binary for the peer processor.
fn main() {
    // SAFETY: The calling process is required to pass a valid file descriptor.
    let fd = unsafe { OwnedFd::from_raw_fd(3) };
    audio_processor::processors::peer::Worker::run(fd);
}
