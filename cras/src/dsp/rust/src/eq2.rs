// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use nix::errno::Errno;

use crate::biquad::Biquad;
use crate::biquad::BiquadType;

/// Maximum number of biquad filters an EQ2 can have per channel
pub const MAX_BIQUADS_PER_EQ2: usize = 10;
pub const EQ2_NUM_CHANNELS: usize = 2;

#[derive(Default)]
/// "eq2" is a two channel version of the "eq" filter. It processes two channels
/// of data at once to increase performance.
pub struct EQ2 {
    pub biquads: Vec<[Biquad; EQ2_NUM_CHANNELS]>,
    pub n: [usize; EQ2_NUM_CHANNELS],
}

impl EQ2 {
    pub fn new() -> Self {
        EQ2 {
            biquads: vec![
                [Biquad::new_set(BiquadType::BQ_NONE, 0., 0., 0.); 2];
                MAX_BIQUADS_PER_EQ2
            ],
            n: [0; EQ2_NUM_CHANNELS],
        }
    }

    pub fn append_biquad(
        &mut self,
        channel: usize,
        enum_type: BiquadType,
        freq: f64,
        q: f64,
        gain: f64,
    ) -> Result<(), Errno> {
        if self.n[channel] >= MAX_BIQUADS_PER_EQ2 {
            return Err(Errno::EINVAL);
        }
        let bq = Biquad::new_set(enum_type, freq, q, gain);
        self.biquads[self.n[channel]][channel] = bq;
        self.n[channel] += 1;
        Ok(())
    }

    pub fn append_biquad_direct(&mut self, channel: usize, biquad: Biquad) -> Result<(), Errno> {
        if self.n[channel] >= MAX_BIQUADS_PER_EQ2 {
            return Err(Errno::EINVAL);
        }
        self.biquads[self.n[channel]][channel] = biquad;
        self.n[channel] += 1;
        Ok(())
    }

    pub fn process_one(bqs: &mut [Biquad; EQ2_NUM_CHANNELS], data0: &mut [f32], data1: &mut [f32]) {
        let (left, right) = bqs.split_at_mut(1);
        let qL = &mut left[0];
        let qR = &mut right[0];

        let mut x1L: f32 = qL.x1;
        let mut x2L: f32 = qL.x2;
        let mut y1L: f32 = qL.y1;
        let mut y2L: f32 = qL.y2;
        let b0L: f32 = qL.b0;
        let b1L: f32 = qL.b1;
        let b2L: f32 = qL.b2;
        let a1L: f32 = qL.a1;
        let a2L: f32 = qL.a2;

        let mut x1R: f32 = qR.x1;
        let mut x2R: f32 = qR.x2;
        let mut y1R: f32 = qR.y1;
        let mut y2R: f32 = qR.y2;
        let b0R: f32 = qR.b0;
        let b1R: f32 = qR.b1;
        let b2R: f32 = qR.b2;
        let a1R: f32 = qR.a1;
        let a2R: f32 = qR.a2;

        for (xL, xR) in std::iter::zip(data0, data1) {
            let yL = b0L * (*xL) + b1L * x1L + b2L * x2L - a1L * y1L - a2L * y2L;
            x2L = x1L;
            x1L = *xL;
            y2L = y1L;
            y1L = yL;

            let yR = b0R * (*xR) + b1R * x1R + b2R * x2R - a1R * y1R - a2R * y2R;
            x2R = x1R;
            x1R = *xR;
            y2R = y1R;
            y1R = yR;

            *xL = yL;
            *xR = yR;
        }

        qL.x1 = x1L;
        qL.x2 = x2L;
        qL.y1 = y1L;
        qL.y2 = y2L;
        qR.x1 = x1R;
        qR.x2 = x2R;
        qR.y1 = y1R;
        qR.y2 = y2R;
    }

    pub fn process(&mut self, data0: &mut [f32], data1: &mut [f32]) {
        let n: usize = self.n[0].max(self.n[1]);
        for bqs in self.biquads.iter_mut().take(n) {
            Self::process_one(bqs, data0, data1);
        }
    }
}
