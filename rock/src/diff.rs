// Copyright 2025 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use std::path::PathBuf;

use anyhow::ensure;
use audio_processor::util::read_wav;
use audio_processor::MultiBuffer;
use clap::Args;
use float_cmp::approx_eq;

#[derive(Args)]
pub(crate) struct DiffCommand {
    /// Path to the first WAVE file
    a: PathBuf,
    /// Path to the second WAVE file
    b: PathBuf,
}

impl DiffCommand {
    pub(crate) fn run(&self) -> anyhow::Result<()> {
        let (a_spec, a_buf) = read_wav(&self.a)?;
        let (b_spec, b_buf) = read_wav(&self.b)?;
        diff(&a_spec, &a_buf, &b_spec, &b_buf)
    }
}

fn diff(
    a_spec: &hound::WavSpec,
    a_buf: &MultiBuffer<f32>,
    b_spec: &hound::WavSpec,
    b_buf: &MultiBuffer<f32>,
) -> anyhow::Result<()> {
    ensure!(
        a_spec.channels == b_spec.channels,
        "Channel mismatch: {} != {}",
        a_spec.channels,
        b_spec.channels
    );
    ensure!(
        a_spec.sample_rate == b_spec.sample_rate,
        "Sample rate mismatch: {} != {}",
        a_spec.sample_rate,
        b_spec.sample_rate
    );
    ensure!(
        a_buf[0].len() == b_buf[0].len(),
        "Sample length mismatch: {} != {}",
        a_buf[0].len(),
        b_buf[0].len()
    );

    for i in 0..a_spec.channels as usize {
        let a_slice = &a_buf[i];
        let b_slice = &b_buf[i];
        ensure!(
            a_slice
                .iter()
                .zip(b_slice.iter())
                .all(|(a, b)| approx_eq!(f32, *a, *b)),
            "Audio contents differ"
        );
    }
    return Ok(());
}

#[cfg(test)]
mod tests {
    use audio_processor::MultiBuffer;
    use hound::WavSpec;

    use super::*;

    fn new_wav_spec(channels: u16, sample_rate: u32) -> WavSpec {
        WavSpec {
            channels,
            sample_rate,
            bits_per_sample: 32,
            sample_format: hound::SampleFormat::Float,
        }
    }

    #[test]
    fn test_diff_identical_files() {
        let spec = new_wav_spec(1, 48000);
        let buffer = MultiBuffer::from(vec![vec![1.0, 2.0, 3.0]]);
        assert!(diff(&spec, &buffer, &spec, &buffer).is_ok());
    }

    #[test]
    fn test_diff_different_channels() {
        let spec_a = new_wav_spec(2, 48000);
        let spec_b = new_wav_spec(1, 48000);
        let buffer_a = MultiBuffer::from(vec![vec![1.0, 2.0], vec![3.0, 4.0]]);
        let buffer_b = MultiBuffer::from(vec![vec![1.0, 2.0]]);

        // The diff should fail due to spec mismatch.
        assert!(diff(&spec_a, &buffer_a, &spec_b, &buffer_b).is_err());
    }

    #[test]
    fn test_diff_different_sample_rates() {
        let spec_a = new_wav_spec(1, 24000);
        let spec_b = new_wav_spec(1, 48000);
        let buffer_a = MultiBuffer::from(vec![vec![1.0, 2.0]]);
        let buffer_b = MultiBuffer::from(vec![vec![1.0, 2.0]]);

        // The diff should fail due to spec mismatch.
        assert!(diff(&spec_a, &buffer_a, &spec_b, &buffer_b).is_err());
    }

    #[test]
    fn test_diff_different_shapes() {
        let spec_a = new_wav_spec(1, 48000);
        let spec_b = new_wav_spec(1, 48000);
        let buffer_a = MultiBuffer::from(vec![vec![1.0, 2.0]]);
        let buffer_b = MultiBuffer::from(vec![vec![1.0, 2.0, 3.0]]);

        // The diff should fail due to shape mismatch.
        assert!(diff(&spec_a, &buffer_a, &spec_b, &buffer_b).is_err());
    }

    #[test]
    fn test_diff_different_content() {
        let spec = new_wav_spec(1, 48000);
        let buffer_a = MultiBuffer::from(vec![vec![1.0, 2.0, 3.0]]);
        let buffer_b = MultiBuffer::from(vec![vec![1.0, 2.0, 4.0]]);

        // The diff should fail due to content difference.
        assert!(diff(&spec, &buffer_a, &spec, &buffer_b).is_err());
    }

    #[test]
    fn test_diff_approximately_equal_content() {
        let spec = new_wav_spec(1, 48000);
        let buffer_a = MultiBuffer::from(vec![vec![1.234567, 0.0, -9.876543]]);
        let buffer_b = MultiBuffer::from(vec![vec![1.2345671, 0.00000001, -9.876543]]);

        // The diff should succeed because `approx_eq` handles floating-point precision.
        assert!(diff(&spec, &buffer_a, &spec, &buffer_b).is_ok());
    }
}
