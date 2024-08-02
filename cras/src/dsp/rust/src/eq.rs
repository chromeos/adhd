// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use nix::errno::Errno;

use crate::biquad::Biquad;
use crate::biquad::BiquadType;

pub const MAX_BIQUADS_PER_EQ: usize = 10;

pub struct EQ {
    biquads: Vec<Biquad>,
}

impl EQ {
    pub fn new() -> Self {
        EQ {
            biquads: Vec::new(),
        }
    }

    pub fn append_biquad(
        &mut self,
        enum_type: BiquadType,
        freq: f64,
        q: f64,
        gain: f64,
    ) -> Result<(), Errno> {
        if self.biquads.len() >= MAX_BIQUADS_PER_EQ {
            return Err(Errno::EINVAL);
        }
        let bq = Biquad::new_set(enum_type, freq, q, gain);
        self.biquads.push(bq);
        Ok(())
    }

    pub fn append_biquad_direct(&mut self, biquad: &Biquad) -> Result<(), Errno> {
        if self.biquads.len() >= MAX_BIQUADS_PER_EQ {
            return Err(Errno::EINVAL);
        }
        self.biquads.push((*biquad).clone());
        Ok(())
    }

    #[allow(dead_code)]
    // This is the prototype of the processing loop.
    pub fn process1(&mut self, data: &mut [f32]) {
        for q in self.biquads.iter_mut() {
            let b0: f32 = (*q).b0;
            let b1: f32 = (*q).b1;
            let b2: f32 = (*q).b2;
            let a1: f32 = (*q).a1;
            let a2: f32 = (*q).a2;
            let mut x1: f32 = (*q).x1;
            let mut x2: f32 = (*q).x2;
            let mut y1: f32 = (*q).y1;
            let mut y2: f32 = (*q).y2;
            for x in &mut *data {
                let y: f32 = b0 * (*x) + b1 * x1 + b2 * x2 - a1 * y1 - a2 * y2;
                *x = y;
                x2 = x1;
                x1 = *x;
                y2 = y1;
                y1 = y;
            }
            q.x1 = x1;
            q.x2 = x2;
            q.y1 = y1;
            q.y2 = y2;
        }
    }

    // This is the actual processing loop used. It is the unrolled version of the
    // above prototype.
    pub fn process(&mut self, data: &mut [f32]) {
        for iter in self.biquads.chunks_mut(2) {
            if iter.len() == 1 {
                let q = &mut iter[0];
                let mut x1: f32 = q.x1;
                let mut x2: f32 = q.x2;
                let mut y1: f32 = q.y1;
                let mut y2: f32 = q.y2;
                let b0: f32 = q.b0;
                let b1: f32 = q.b1;
                let b2: f32 = q.b2;
                let a1: f32 = q.a1;
                let a2: f32 = q.a2;
                for x in &mut *data {
                    let y: f32 = b0 * (*x) + b1 * x1 + b2 * x2 - a1 * y1 - a2 * y2;
                    x2 = x1;
                    x1 = *x;
                    y2 = y1;
                    y1 = y;
                    *x = y;
                }
                q.x1 = x1;
                q.x2 = x2;
                q.y1 = y1;
                q.y2 = y2;
            } else {
                let (left, right) = iter.split_at_mut(1);
                let q = &mut left[0];
                let r = &mut right[0];
                let mut x1: f32 = q.x1;
                let mut x2: f32 = q.x2;
                let mut y1: f32 = q.y1;
                let mut y2: f32 = q.y2;
                let qb0: f32 = q.b0;
                let qb1: f32 = q.b1;
                let qb2: f32 = q.b2;
                let qa1: f32 = q.a1;
                let qa2: f32 = q.a2;

                let mut z1: f32 = r.y1;
                let mut z2: f32 = r.y2;
                let rb0: f32 = r.b0;
                let rb1: f32 = r.b1;
                let rb2: f32 = r.b2;
                let ra1: f32 = r.a1;
                let ra2: f32 = r.a2;

                for x in &mut *data {
                    let y: f32 = qb0 * (*x) + qb1 * x1 + qb2 * x2 - qa1 * y1 - qa2 * y2;
                    let z: f32 = rb0 * y + rb1 * y1 + rb2 * y2 - ra1 * z1 - ra2 * z2;
                    x2 = x1;
                    x1 = *x;
                    y2 = y1;
                    y1 = y;
                    z2 = z1;
                    z1 = z;
                    *x = z;
                }
                q.x1 = x1;
                q.x2 = x2;
                q.y1 = y1;
                q.y2 = y2;
                r.y1 = z1;
                r.y2 = z2;
            }
        }
    }
}
