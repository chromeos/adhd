// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::biquad::Biquad;
use crate::biquad::BiquadType;

#[derive(Clone, Default)]
#[repr(C)]
/// An LR4 filter is two biquads with the same parameters connected in series:
///
/// ```text
/// x -- [BIQUAD] -- y -- [BIQUAD] -- z
/// ```
///
/// Both biquad filter has the same parameter b[012] and a[12],
/// The variable [xyz][12] keep the history values.
pub struct LR4 {
    b0: f32,
    b1: f32,
    b2: f32,
    a1: f32,
    a2: f32,
    x1: f32,
    x2: f32,
    y1: f32,
    y2: f32,
    z1: f32,
    z2: f32,
}

impl LR4 {
    pub fn new(enum_type: BiquadType, freq: f32) -> Self {
        let bq: Biquad = Biquad::new_set(enum_type, freq as f64, 0_f64, 0_f64);
        LR4 {
            b0: bq.b0,
            b1: bq.b1,
            b2: bq.b2,
            a1: bq.a1,
            a2: bq.a2,
            x1: 0_f32,
            x2: 0_f32,
            y1: 0_f32,
            y2: 0_f32,
            z1: 0_f32,
            z2: 0_f32,
        }
    }

    /// Split input data using two LR4 filters, put the result into the input array
    /// and another array.
    ///
    /// ```text
    /// data0 --+-- lp --> data0
    ///         |
    ///         \-- hp --> data1
    /// ```
    ///
    pub fn split(lp: &mut LR4, hp: &mut LR4, data0: &mut [f32], data1: &mut [f32]) {
        let mut lx1: f32 = lp.x1;
        let mut lx2: f32 = lp.x2;
        let mut ly1: f32 = lp.y1;
        let mut ly2: f32 = lp.y2;
        let mut lz1: f32 = lp.z1;
        let mut lz2: f32 = lp.z2;
        let lb0: f32 = lp.b0;
        let lb1: f32 = lp.b1;
        let lb2: f32 = lp.b2;
        let la1: f32 = lp.a1;
        let la2: f32 = lp.a2;

        let mut hx1: f32 = hp.x1;
        let mut hx2: f32 = hp.x2;
        let mut hy1: f32 = hp.y1;
        let mut hy2: f32 = hp.y2;
        let mut hz1: f32 = hp.z1;
        let mut hz2: f32 = hp.z2;
        let hb0: f32 = hp.b0;
        let hb1: f32 = hp.b1;
        let hb2: f32 = hp.b2;
        let ha1: f32 = hp.a1;
        let ha2: f32 = hp.a2;

        for (data0_i, data1_i) in std::iter::zip(data0, data1) {
            let x: f32 = *data0_i;
            let mut y: f32 = lb0 * x + lb1 * lx1 + lb2 * lx2 - la1 * ly1 - la2 * ly2;
            let mut z: f32 = lb0 * y + lb1 * ly1 + lb2 * ly2 - la1 * lz1 - la2 * lz2;
            lx2 = lx1;
            lx1 = x;
            ly2 = ly1;
            ly1 = y;
            lz2 = lz1;
            lz1 = z;
            (*data0_i) = z;

            y = hb0 * x + hb1 * hx1 + hb2 * hx2 - ha1 * hy1 - ha2 * hy2;
            z = hb0 * y + hb1 * hy1 + hb2 * hy2 - ha1 * hz1 - ha2 * hz2;
            hx2 = hx1;
            hx1 = x;
            hy2 = hy1;
            hy1 = y;
            hz2 = hz1;
            hz1 = z;
            (*data1_i) = z;
        }

        lp.x1 = lx1;
        lp.x2 = lx2;
        lp.y1 = ly1;
        lp.y2 = ly2;
        lp.z1 = lz1;
        lp.z2 = lz2;

        hp.x1 = hx1;
        hp.x2 = hx2;
        hp.y1 = hy1;
        hp.y2 = hy2;
        hp.z1 = hz1;
        hp.z2 = hz2;
    }

    /// Split input data using two LR4 filters and sum them back to the original
    /// data array.
    ///
    /// ```text
    /// data --+-- lp --+--> data
    ///        |        |
    ///        \-- hp --/
    /// ```
    ///
    pub fn merge(lp: &mut LR4, hp: &mut LR4, data: &mut [f32]) {
        let mut lx1: f32 = lp.x1;
        let mut lx2: f32 = lp.x2;
        let mut ly1: f32 = lp.y1;
        let mut ly2: f32 = lp.y2;
        let mut lz1: f32 = lp.z1;
        let mut lz2: f32 = lp.z2;
        let lb0: f32 = lp.b0;
        let lb1: f32 = lp.b1;
        let lb2: f32 = lp.b2;
        let la1: f32 = lp.a1;
        let la2: f32 = lp.a2;

        let mut hx1: f32 = hp.x1;
        let mut hx2: f32 = hp.x2;
        let mut hy1: f32 = hp.y1;
        let mut hy2: f32 = hp.y2;
        let mut hz1: f32 = hp.z1;
        let mut hz2: f32 = hp.z2;
        let hb0: f32 = hp.b0;
        let hb1: f32 = hp.b1;
        let hb2: f32 = hp.b2;
        let ha1: f32 = hp.a1;
        let ha2: f32 = hp.a2;

        for x in data.iter_mut() {
            let mut y: f32 = lb0 * (*x) + lb1 * lx1 + lb2 * lx2 - la1 * ly1 - la2 * ly2;
            let mut z: f32 = lb0 * y + lb1 * ly1 + lb2 * ly2 - la1 * lz1 - la2 * lz2;
            lx2 = lx1;
            lx1 = (*x);
            ly2 = ly1;
            ly1 = y;
            lz2 = lz1;
            lz1 = z;

            y = hb0 * (*x) + hb1 * hx1 + hb2 * hx2 - ha1 * hy1 - ha2 * hy2;
            z = hb0 * y + hb1 * hy1 + hb2 * hy2 - ha1 * hz1 - ha2 * hz2;
            hx2 = hx1;
            hx1 = (*x);
            hy2 = hy1;
            hy1 = y;
            hz2 = hz1;
            hz1 = z;
            (*x) = z + lz1;
        }

        lp.x1 = lx1;
        lp.x2 = lx2;
        lp.y1 = ly1;
        lp.y2 = ly2;
        lp.z1 = lz1;
        lp.z2 = lz2;

        hp.x1 = hx1;
        hp.x2 = hx2;
        hp.y1 = hy1;
        hp.y2 = hy2;
        hp.z1 = hz1;
        hp.z2 = hz2;
    }
}

#[derive(Default)]
#[repr(C)]
/// Three bands crossover filter:
///
/// ```text
/// INPUT --+-- lp0 --+-- lp1 --+---> LOW (0)
///         |         |         |
///         |         \-- hp1 --/
///         |
///         \-- hp0 --+-- lp2 ------> MID (1)
///                   |
///                   \-- hp2 ------> HIGH (2)
///
///            [f0]       [f1]
/// ```
///
/// Each lp or hp is an LR4 filter, which consists of two second-order
/// lowpass or highpass butterworth filters.
pub struct Crossover {
    lp: [LR4; 3],
    hp: [LR4; 3],
}

impl Crossover {
    pub fn new(freq1: f32, freq2: f32) -> Self {
        let mut xo = Self::default();
        for i in 0..3 {
            let f = match i {
                0 => freq1,
                _ => freq2,
            };
            xo.lp[i] = LR4::new(BiquadType::BQ_LOWPASS, f);
            xo.hp[i] = LR4::new(BiquadType::BQ_HIGHPASS, f);
        }
        xo
    }

    pub fn process(&mut self, data0: &mut [f32], data1: &mut [f32], data2: &mut [f32]) {
        LR4::split(&mut self.lp[0], &mut self.hp[0], data0, data1);
        LR4::merge(&mut self.lp[1], &mut self.hp[1], data0);
        LR4::split(&mut self.lp[2], &mut self.hp[2], data1, data2);
    }
}
