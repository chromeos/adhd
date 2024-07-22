// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use itertools::izip;

use crate::biquad::Biquad;
use crate::biquad::BiquadType;

// The number of lp and hp LR4 filter pairs in crossover2
pub const CROSSOVER2_NUM_LR4_PAIRS: usize = 3;

#[repr(C)]
#[derive(Default)]
/// An LR4 filter is two biquads with the same parameters connected in series:
///
///```text
/// x -- [BIQUAD] -- y -- [BIQUAD] -- z
/// ```
///
/// Both biquad filter has the same parameter b[012] and a[12],
/// The variable [xyz][12][LR] keep the history values.
///
pub struct LR42 {
    b0: f32,
    b1: f32,
    b2: f32,
    a1: f32,
    a2: f32,
    x1L: f32,
    x1R: f32,
    x2L: f32,
    x2R: f32,
    y1L: f32,
    y1R: f32,
    y2L: f32,
    y2R: f32,
    z1L: f32,
    z1R: f32,
    z2L: f32,
    z2R: f32,
}

impl LR42 {
    fn new(enum_type: BiquadType, freq: f64) -> Self {
        let q = Biquad::new_set(enum_type, freq, 0., 0.);
        LR42 {
            b0: q.b0,
            b1: q.b1,
            b2: q.b2,
            a1: q.a1,
            a2: q.a2,
            x1L: 0.,
            x1R: 0.,
            x2L: 0.,
            x2R: 0.,
            y1L: 0.,
            y1R: 0.,
            y2L: 0.,
            y2R: 0.,
            z1L: 0.,
            z1R: 0.,
            z2L: 0.,
            z2R: 0.,
        }
    }

    /// Split input data using two LR4 filters, put the result into the input array
    /// and another array.
    ///
    ///```text
    /// data0 --+-- lp --> data0
    ///         |
    ///         \-- hp --> data1
    /// ```
    ///
    fn split(
        lp: &mut LR42,
        hp: &mut LR42,
        data0L: &mut [f32],
        data0R: &mut [f32],
        data1L: &mut [f32],
        data1R: &mut [f32],
    ) {
        let mut lx1L: f32 = lp.x1L;
        let mut lx1R: f32 = lp.x1R;
        let mut lx2L: f32 = lp.x2L;
        let mut lx2R: f32 = lp.x2R;
        let mut ly1L: f32 = lp.y1L;
        let mut ly1R: f32 = lp.y1R;
        let mut ly2L: f32 = lp.y2L;
        let mut ly2R: f32 = lp.y2R;
        let mut lz1L: f32 = lp.z1L;
        let mut lz1R: f32 = lp.z1R;
        let mut lz2L: f32 = lp.z2L;
        let mut lz2R: f32 = lp.z2R;
        let lb0: f32 = lp.b0;
        let lb1: f32 = lp.b1;
        let lb2: f32 = lp.b2;
        let la1: f32 = lp.a1;
        let la2: f32 = lp.a2;

        let mut hx1L: f32 = hp.x1L;
        let mut hx1R: f32 = hp.x1R;
        let mut hx2L: f32 = hp.x2L;
        let mut hx2R: f32 = hp.x2R;
        let mut hy1L: f32 = hp.y1L;
        let mut hy1R: f32 = hp.y1R;
        let mut hy2L: f32 = hp.y2L;
        let mut hy2R: f32 = hp.y2R;
        let mut hz1L: f32 = hp.z1L;
        let mut hz1R: f32 = hp.z1R;
        let mut hz2L: f32 = hp.z2L;
        let mut hz2R: f32 = hp.z2R;
        let hb0: f32 = hp.b0;
        let hb1: f32 = hp.b1;
        let hb2: f32 = hp.b2;
        let ha1: f32 = hp.a1;
        let ha2: f32 = hp.a2;

        for (data0L_i, data0R_i, data1L_i, data1R_i) in izip!(data0L, data0R, data1L, data1R) {
            let mut xL: f32 = *data0L_i;
            let mut xR: f32 = *data0R_i;
            let mut yL: f32 = lb0 * xL + lb1 * lx1L + lb2 * lx2L - la1 * ly1L - la2 * ly2L;
            let mut yR: f32 = lb0 * xR + lb1 * lx1R + lb2 * lx2R - la1 * ly1R - la2 * ly2R;
            let mut zL: f32 = lb0 * yL + lb1 * ly1L + lb2 * ly2L - la1 * lz1L - la2 * lz2L;
            let mut zR: f32 = lb0 * yR + lb1 * ly1R + lb2 * ly2R - la1 * lz1R - la2 * lz2R;
            lx2L = lx1L;
            lx2R = lx1R;
            lx1L = xL;
            lx1R = xR;
            ly2L = ly1L;
            ly2R = ly1R;
            ly1L = yL;
            ly1R = yR;
            lz2L = lz1L;
            lz2R = lz1R;
            lz1L = zL;
            lz1R = zR;
            *data0L_i = zL;
            *data0R_i = zR;

            yL = hb0 * xL + hb1 * hx1L + hb2 * hx2L - ha1 * hy1L - ha2 * hy2L;
            yR = hb0 * xR + hb1 * hx1R + hb2 * hx2R - ha1 * hy1R - ha2 * hy2R;
            zL = hb0 * yL + hb1 * hy1L + hb2 * hy2L - ha1 * hz1L - ha2 * hz2L;
            zR = hb0 * yR + hb1 * hy1R + hb2 * hy2R - ha1 * hz1R - ha2 * hz2R;
            hx2L = hx1L;
            hx2R = hx1R;
            hx1L = xL;
            hx1R = xR;
            hy2L = hy1L;
            hy2R = hy1R;
            hy1L = yL;
            hy1R = yR;
            hz2L = hz1L;
            hz2R = hz1R;
            hz1L = zL;
            hz1R = zR;
            *data1L_i = zL;
            *data1R_i = zR;
        }

        lp.x1L = lx1L;
        lp.x1R = lx1R;
        lp.x2L = lx2L;
        lp.x2R = lx2R;
        lp.y1L = ly1L;
        lp.y1R = ly1R;
        lp.y2L = ly2L;
        lp.y2R = ly2R;
        lp.z1L = lz1L;
        lp.z1R = lz1R;
        lp.z2L = lz2L;
        lp.z2R = lz2R;

        hp.x1L = hx1L;
        hp.x1R = hx1R;
        hp.x2L = hx2L;
        hp.x2R = hx2R;
        hp.y1L = hy1L;
        hp.y1R = hy1R;
        hp.y2L = hy2L;
        hp.y2R = hy2R;
        hp.z1L = hz1L;
        hp.z1R = hz1R;
        hp.z2L = hz2L;
        hp.z2R = hz2R;
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
    fn merge(lp: &mut LR42, hp: &mut LR42, dataL: &mut [f32], dataR: &mut [f32]) {
        let mut lx1L: f32 = lp.x1L;
        let mut lx1R: f32 = lp.x1R;
        let mut lx2L: f32 = lp.x2L;
        let mut lx2R: f32 = lp.x2R;
        let mut ly1L: f32 = lp.y1L;
        let mut ly1R: f32 = lp.y1R;
        let mut ly2L: f32 = lp.y2L;
        let mut ly2R: f32 = lp.y2R;
        let mut lz1L: f32 = lp.z1L;
        let mut lz1R: f32 = lp.z1R;
        let mut lz2L: f32 = lp.z2L;
        let mut lz2R: f32 = lp.z2R;
        let lb0: f32 = lp.b0;
        let lb1: f32 = lp.b1;
        let lb2: f32 = lp.b2;
        let la1: f32 = lp.a1;
        let la2: f32 = lp.a2;

        let mut hx1L: f32 = hp.x1L;
        let mut hx1R: f32 = hp.x1R;
        let mut hx2L: f32 = hp.x2L;
        let mut hx2R: f32 = hp.x2R;
        let mut hy1L: f32 = hp.y1L;
        let mut hy1R: f32 = hp.y1R;
        let mut hy2L: f32 = hp.y2L;
        let mut hy2R: f32 = hp.y2R;
        let mut hz1L: f32 = hp.z1L;
        let mut hz1R: f32 = hp.z1R;
        let mut hz2L: f32 = hp.z2L;
        let mut hz2R: f32 = hp.z2R;
        let hb0: f32 = hp.b0;
        let hb1: f32 = hp.b1;
        let hb2: f32 = hp.b2;
        let ha1: f32 = hp.a1;
        let ha2: f32 = hp.a2;

        for (xL, xR) in std::iter::zip(dataL, dataR) {
            let mut yL: f32 = lb0 * (*xL) + lb1 * lx1L + lb2 * lx2L - la1 * ly1L - la2 * ly2L;
            let mut yR: f32 = lb0 * (*xR) + lb1 * lx1R + lb2 * lx2R - la1 * ly1R - la2 * ly2R;
            let mut zL: f32 = lb0 * yL + lb1 * ly1L + lb2 * ly2L - la1 * lz1L - la2 * lz2L;
            let mut zR: f32 = lb0 * yR + lb1 * ly1R + lb2 * ly2R - la1 * lz1R - la2 * lz2R;
            lx2L = lx1L;
            lx2R = lx1R;
            lx1L = *xL;
            lx1R = *xR;
            ly2L = ly1L;
            ly2R = ly1R;
            ly1L = yL;
            ly1R = yR;
            lz2L = lz1L;
            lz2R = lz1R;
            lz1L = zL;
            lz1R = zR;

            yL = hb0 * (*xL) + hb1 * hx1L + hb2 * hx2L - ha1 * hy1L - ha2 * hy2L;
            yR = hb0 * (*xR) + hb1 * hx1R + hb2 * hx2R - ha1 * hy1R - ha2 * hy2R;
            zL = hb0 * yL + hb1 * hy1L + hb2 * hy2L - ha1 * hz1L - ha2 * hz2L;
            zR = hb0 * yR + hb1 * hy1R + hb2 * hy2R - ha1 * hz1R - ha2 * hz2R;
            hx2L = hx1L;
            hx2R = hx1R;
            hx1L = *xL;
            hx1R = *xR;
            hy2L = hy1L;
            hy2R = hy1R;
            hy1L = yL;
            hy1R = yR;
            hz2L = hz1L;
            hz2R = hz1R;
            hz1L = zL;
            hz1R = zR;
            *xL = zL + lz1L;
            *xR = zR + lz1R;
        }

        lp.x1L = lx1L;
        lp.x1R = lx1R;
        lp.x2L = lx2L;
        lp.x2R = lx2R;
        lp.y1L = ly1L;
        lp.y1R = ly1R;
        lp.y2L = ly2L;
        lp.y2R = ly2R;
        lp.z1L = lz1L;
        lp.z1R = lz1R;
        lp.z2L = lz2L;
        lp.z2R = lz2R;

        hp.x1L = hx1L;
        hp.x1R = hx1R;
        hp.x2L = hx2L;
        hp.x2R = hx2R;
        hp.y1L = hy1L;
        hp.y1R = hy1R;
        hp.y2L = hy2L;
        hp.y2R = hy2R;
        hp.z1L = hz1L;
        hp.z1R = hz1R;
        hp.z2L = hz2L;
        hp.z2R = hz2R;
    }
}

#[repr(C)]
#[derive(Default)]
/// Three bands crossover filter:
///
///```text
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
///
pub struct Crossover2 {
    lp: [LR42; CROSSOVER2_NUM_LR4_PAIRS],
    hp: [LR42; CROSSOVER2_NUM_LR4_PAIRS],
}

impl Crossover2 {
    pub fn init(&mut self, freq1: f64, freq2: f64) {
        for i in 0..CROSSOVER2_NUM_LR4_PAIRS {
            let f = match i {
                0 => freq1,
                _ => freq2,
            };
            self.lp[i] = LR42::new(BiquadType::BQ_LOWPASS, f);
            self.hp[i] = LR42::new(BiquadType::BQ_HIGHPASS, f);
        }
    }

    pub fn process(
        &mut self,
        data0L: &mut [f32],
        data0R: &mut [f32],
        data1L: &mut [f32],
        data1R: &mut [f32],
        data2L: &mut [f32],
        data2R: &mut [f32],
    ) {
        LR42::split(
            &mut self.lp[0],
            &mut self.hp[0],
            data0L,
            data0R,
            data1L,
            data1R,
        );
        LR42::merge(&mut self.lp[1], &mut self.hp[1], data0L, data0R);
        LR42::split(
            &mut self.lp[2],
            &mut self.hp[2],
            data1L,
            data1R,
            data2L,
            data2R,
        );
    }
}
