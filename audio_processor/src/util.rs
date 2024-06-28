// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use std::path::Path;

use anyhow::ensure;
use anyhow::Context;
use hound::WavReader;
use nix::sys::resource::setrlimit;
use nix::sys::resource::Resource;

use crate::MultiBuffer;
use crate::Sample;

/// Read the WAVE file at path into a single MultiBuffer<T>.
pub fn read_wav<T: Sample + hound::Sample>(
    path: &Path,
) -> anyhow::Result<(hound::WavSpec, MultiBuffer<T>)> {
    let r = WavReader::open(path).context("WavReader::open")?;
    let spec = r.spec();
    let channels = spec.channels as usize;
    let frames = r.len() as usize / channels;
    let mut out = MultiBuffer::<T>::new_equilibrium(crate::Shape { channels, frames });
    for (i, sample) in r.into_samples().enumerate() {
        out[i % channels][i / channels] = sample?;
    }

    Ok((spec, out))
}

// TODO(b/268271100): Call the C version when we can build C code before Rust.
pub fn set_thread_priority() -> anyhow::Result<()> {
    // CRAS_SERVER_RT_THREAD_PRIORITY 12
    let p = 12;
    setrlimit(Resource::RLIMIT_RTPRIO, p, p).context("setrlimit")?;

    // SAFETY: sched_param is properly initialized.
    unsafe {
        let sched_param = libc::sched_param {
            sched_priority: p as i32,
        };
        let rc = libc::pthread_setschedparam(libc::pthread_self(), libc::SCHED_RR, &sched_param);
        ensure!(rc == 0, "pthread_setschedparam returned {rc}");
    }

    Ok(())
}
