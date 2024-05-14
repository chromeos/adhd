// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use std::path::Path;

use anyhow::Context;
use hound::WavReader;

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
