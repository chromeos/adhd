// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use std::path::Path;

use anyhow::bail;
use anyhow::Context;
use audio_processor::processors::WavSource;
use audio_processor::AudioProcessor;
use audio_processor::MultiBuffer;
use audio_processor::Shape;

pub(crate) fn read_wav_expect_len_at_least(
    path: &Path,
    len_at_least: usize,
) -> anyhow::Result<(hound::WavSpec, MultiBuffer<f32>)> {
    let (spec, buffer) = read_wav(path)?;
    if buffer.data[0].len() < len_at_least {
        bail!(
            "{} is too short! Expected at least {} frames but got {}",
            path.display(),
            len_at_least,
            buffer.data[0].len()
        );
    }
    Ok((spec, buffer))
}

/// Read the WAVE file at path into a single MultiBuffer<f32>.
pub(crate) fn read_wav(path: &Path) -> anyhow::Result<(hound::WavSpec, MultiBuffer<f32>)> {
    let reader =
        hound::WavReader::open(path).with_context(|| format!("cannot open {}", path.display()))?;
    let samples = reader.duration();
    let spec = reader.spec();
    let mut source = WavSource::new(reader, samples as usize);
    let mut empty = MultiBuffer::new(Shape {
        channels: 1,
        frames: 0,
    });
    let view = source
        .process(empty.as_multi_slice())
        .with_context(|| format!("cannot read {}", path.display()))?;
    Ok((spec, MultiBuffer::from(view)))
}
