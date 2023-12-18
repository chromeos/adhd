// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use std::path::PathBuf;

use anyhow::bail;
use clap::Args;

use crate::wav::read_wav;

#[derive(Args)]
pub(crate) struct CosineCommand {
    /// Path to the first WAVE file
    a: PathBuf,
    /// Path to the second WAVE file
    b: PathBuf,
    /// Delay of b in frames.
    #[clap(long, default_value = "0")]
    delay: i32,
}

impl CosineCommand {
    pub(crate) fn run(&self) -> anyhow::Result<()> {
        let (a_spec, a_buf) = read_wav(&self.a)?;
        let (b_spec, b_buf) = read_wav(&self.b)?;
        if a_spec.sample_rate != a_spec.sample_rate {
            bail!(
                "sample rate mismatch: {} != {}",
                a_spec.sample_rate,
                b_spec.sample_rate
            );
        }

        let val = cosine_with_delay(&a_buf[0], &b_buf[0], self.delay);
        println!("{val}");

        Ok(())
    }
}

/// Compute the cosine similarity of `aa` ` and `bb`.
/// The longer one is truncated.
fn cosine(aa: &[f32], bb: &[f32]) -> f64 {
    let mut dot = 0f64;
    let mut a_norm = 0f64;
    let mut b_norm = 0f64;

    for (a, b) in aa.iter().zip(bb) {
        let a = *a as f64;
        let b = *b as f64;
        dot += a * b;
        a_norm += a * a;
        b_norm += b * b;
    }

    dot / a_norm.sqrt() / b_norm.sqrt()
}

fn cosine_with_delay(aa: &[f32], bb: &[f32], delay: i32) -> f64 {
    if delay > 0 {
        cosine(aa, &bb[delay as usize..])
    } else {
        cosine(&aa[-delay as usize..], bb)
    }
}

#[cfg(test)]
mod tests {
    use super::cosine;
    use super::cosine_with_delay;

    #[test]
    fn test_cosine() {
        assert_eq!(cosine(&[1.], &[1.]), 1.);
        assert_eq!(cosine(&[1., 2.], &[1., 2.]), 1.);
        assert_eq!(cosine(&[1., 2.], &[-1., -2.]), -1.);
        assert_eq!(cosine(&[1., 2.], &[2., -1.]), 0.);
        assert!(cosine(&[1.], &[0.]).is_nan());
        assert!(cosine(&[1.], &[]).is_nan());
        assert!(cosine(&[], &[]).is_nan());
    }

    #[test]
    fn test_cosine_with_delay() {
        let aa = [1., 2., 3., 4., 5.];
        let bb = [2., 3., 4., 5., 6.];

        assert_eq!(cosine_with_delay(&aa, &bb, -1), cosine(&aa[1..], &bb));
        assert!((cosine_with_delay(&aa, &bb, -1) - 1.).abs() < 1e-6);
        assert_eq!(cosine_with_delay(&aa, &bb, 1), cosine(&aa, &bb[1..]));
    }
}
