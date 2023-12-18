// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use std::path::PathBuf;

use anyhow::bail;
use clap::Args;
use rustfft::num_complex::Complex;
use rustfft::FftPlanner;

use crate::wav::read_wav_expect_len_at_least;

#[derive(Args)]
pub(crate) struct DelayCommand {
    /// Path to the first WAVE file
    a: PathBuf,
    /// Path to the second WAVE file
    b: PathBuf,
}

impl DelayCommand {
    pub(crate) fn run(&self) -> anyhow::Result<()> {
        let window_size = 20;

        let (aspec, a) = read_wav_expect_len_at_least(&self.a, window_size)?;
        let (bspec, b) = read_wav_expect_len_at_least(&self.b, window_size)?;
        if aspec.sample_rate != bspec.sample_rate {
            bail!(
                "sample rate mismatch: {} != {}",
                aspec.sample_rate,
                bspec.sample_rate
            );
        }

        // Only handle channel 0 for now.
        let astddev: Vec<f32> = a.data[0].windows(window_size).map(pstddev).collect();
        let bstddev: Vec<f32> = b.data[0].windows(window_size).map(pstddev).collect();

        let lag_frames = lag(&astddev, &bstddev);
        println!("{lag_frames}");

        Ok(())
    }
}

/// Computes the population standard deviation of data.
fn pstddev(data: &[f32]) -> f32 {
    let count = data.len() as f64;
    let mean = data.iter().map(|&x| x as f64).sum::<f64>() / count;
    let var = data
        .iter()
        .map(|&x| (x as f64 - mean).powf(2.))
        .sum::<f64>();
    (var / count).sqrt() as f32
}

/// Computes the lag of y compared to x.
/// If a signal shows up at x[i] and y[i+n], then the lag value is n.
/// The lag of x = [1, 0, 0, 0], y = [0, 0, 1, 0] is 2.
fn lag(x: &[f32], y: &[f32]) -> i32 {
    let corr = correlate(y, x);

    let lag = corr
        .iter()
        .enumerate()
        .reduce(|(i, x), (j, y)| if x > y { (i, x) } else { (j, y) })
        .unwrap()
        .0;

    if lag < y.len() {
        lag as i32
    } else {
        lag as i32 - corr.len() as i32
    }
}

/// See:
/// scipy.signal.correlate - https://docs.scipy.org/doc/scipy/reference/generated/scipy.signal.correlate.html
/// https://dsp.stackexchange.com/a/740
fn correlate(x: &[f32], y: &[f32]) -> Vec<f32> {
    let len = x.len() + y.len() - 1;

    // Pad x and y to to len.
    let mut xc = zpad(x, len);
    let mut yc = zpad(y, len);

    let mut rfftp = FftPlanner::new();
    let fft = rfftp.plan_fft_forward(len);
    let mut scratch = vec![Default::default(); fft.get_inplace_scratch_len()];

    // fft(x)
    fft.process_with_scratch(&mut xc, &mut scratch);

    // fft(y)
    fft.process_with_scratch(&mut yc, &mut scratch);

    // conj(fft(y))
    for y in yc.iter_mut() {
        *y = y.conj();
    }

    // fft(x) * conj(fft(y))
    for (x, y) in xc.iter_mut().zip(yc.iter()) {
        *x *= y;
    }

    // ifft(...)
    let ifft = rfftp.plan_fft_inverse(len);
    scratch.resize(ifft.get_inplace_scratch_len(), Default::default());
    ifft.process_with_scratch(&mut xc, &mut scratch);

    // ifft(...) / len
    xc.iter().map(|x| x.re / len as f32).collect()
}

/// Pad x with zeros to len.
fn zpad(x: &[f32], len: usize) -> Vec<Complex<f32>> {
    x.iter()
        .copied()
        .chain(std::iter::repeat(0f32))
        .take(len)
        .map(Complex::<f32>::from)
        .collect()
}

#[cfg(test)]
mod tests {
    #[test]
    fn test_stddev() {
        assert_eq!(
            super::pstddev(&[1., 2., 3., 6., 8.]),
            (34f32 / 5.).sqrt() as f32
        );
    }

    #[test]
    fn test_lag() {
        assert_eq!(super::lag(&[1., 2., -1., -2.], &[0., 1., 2., -1.]), 1);
        assert_eq!(super::lag(&[0., 1., 2., -1.], &[1., 2., -1., -2.]), -1);
        assert_eq!(super::lag(&[1., 2., -1., -2.], &[0., 1., 2., -1., -2.]), 1);
        assert_eq!(super::lag(&[0., 1., 2., -1., -2.], &[1., 2., -1., -2.]), -1);
    }
}
