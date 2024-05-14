// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use std::path::Path;

use anyhow::bail;
use audio_processor::util::read_wav;
use audio_processor::MultiBuffer;

pub(crate) fn read_wav_expect_len_at_least(
    path: &Path,
    len_at_least: usize,
) -> anyhow::Result<(hound::WavSpec, MultiBuffer<f32>)> {
    let (spec, buffer) = read_wav(path)?;
    if buffer[0].len() < len_at_least {
        bail!(
            "{} is too short! Expected at least {} frames but got {}",
            path.display(),
            len_at_least,
            buffer[0].len()
        );
    }
    Ok((spec, buffer))
}
