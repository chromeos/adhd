// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use std::f64::consts::PI;

/* The biquad filter parameters. The transfer function H(z) is (b0 + b1 * z^(-1)
 * + b2 * z^(-2)) / (1 + a1 * z^(-1) + a2 * z^(-2)).  The previous two inputs
 * are stored in x1 and x2, and the previous two outputs are stored in y1 and
 * y2.
 *
 * We use double during the coefficients calculation for better accuracy, but
 * float is used during the actual filtering for faster computation.
 */
#[derive(Copy, Clone)]
#[repr(C)]
pub struct Biquad {
    pub b0: f32,
    pub b1: f32,
    pub b2: f32,
    pub a1: f32,
    pub a2: f32,
    pub x1: f32,
    pub x2: f32,
    pub y1: f32,
    pub y2: f32,
}

#[repr(C)]
pub enum BiquadType {
    // BQ_NONE stands for default, which is an identity filter.
    BQ_NONE,
    BQ_LOWPASS,
    BQ_HIGHPASS,
    BQ_BANDPASS,
    BQ_LOWSHELF,
    BQ_HIGHSHELF,
    BQ_PEAKING,
    BQ_NOTCH,
    BQ_ALLPASS,
}

impl Biquad {
    pub fn new() -> Self {
        Biquad {
            b0: 0.,
            b1: 0.,
            b2: 0.,
            a1: 0.,
            a2: 0.,
            x1: 0.,
            x2: 0.,
            y1: 0.,
            y2: 0.,
        }
    }

    fn new_coefficient(b0: f64, b1: f64, b2: f64, a0: f64, a1: f64, a2: f64) -> Self {
        let mut bq = Self::new();
        let a0_inv = 1. / a0;
        bq.b0 = (b0 * a0_inv) as f32;
        bq.b1 = (b1 * a0_inv) as f32;
        bq.b2 = (b2 * a0_inv) as f32;
        bq.a1 = (a1 * a0_inv) as f32;
        bq.a2 = (a2 * a0_inv) as f32;
        bq
    }

    fn new_lowpass(mut cutoff: f64, mut resonance: f64) -> Self {
        // Limit cutoff to 0 to 1.
        cutoff = cutoff.max(0.).min(1.);

        if cutoff == 1. || cutoff == 0. {
            /* When cutoff is 1, the z-transform is 1.
             * When cutoff is zero, nothing gets through the filter, so set
             * coefficients up correctly.
             */
            return Self::new_coefficient(cutoff, 0., 0., 1., 0., 0.);
        }

        // Compute biquad coefficients for lowpass filter
        resonance = resonance.max(0.); // can't go negative
        let g = 10_f64.powf(0.05 * resonance);
        let d = ((4. - (16. - 16. / (g * g)).sqrt()) / 2.).sqrt();

        let theta = PI * cutoff;
        let sn = 0.5 * d * theta.sin();
        let beta = 0.5 * (1. - sn) / (1. + sn);
        let gamma = (0.5 + beta) * theta.cos();
        let alpha = 0.25 * (0.5 + beta - gamma);

        let b0 = 2. * alpha;
        let b1 = 2. * 2. * alpha;
        let b2 = 2. * alpha;
        let a1 = 2. * -gamma;
        let a2 = 2. * beta;

        Self::new_coefficient(b0, b1, b2, 1., a1, a2)
    }

    fn new_highpass(mut cutoff: f64, mut resonance: f64) -> Self {
        // Limit cutoff to 0 to 1.
        cutoff = cutoff.max(0.).min(1.);

        if cutoff == 1. || cutoff == 0. {
            // When cutoff is one, the z-transform is 0.
            /* When cutoff is zero, we need to be careful because the above
             * gives a quadratic divided by the same quadratic, with poles
             * and zeros on the unit circle in the same place. When cutoff
             * is zero, the z-transform is 1.
             */
            return Self::new_coefficient(1. - cutoff, 0., 0., 1., 0., 0.);
        }

        // Compute biquad coefficients for highpass filter
        resonance = resonance.max(0.); // can't go negative
        let g = 10_f64.powf(0.05 * resonance);
        let d = ((4. - (16. - 16. / (g * g)).sqrt()) / 2.).sqrt();

        let theta = PI * cutoff;
        let sn = 0.5 * d * theta.sin();
        let beta = 0.5 * (1. - sn) / (1. + sn);
        let gamma = (0.5 + beta) * theta.cos();
        let alpha = 0.25 * (0.5 + beta + gamma);

        let b0 = 2. * alpha;
        let b1 = 2. * -2. * alpha;
        let b2 = 2. * alpha;
        let a1 = 2. * -gamma;
        let a2 = 2. * beta;

        Self::new_coefficient(b0, b1, b2, 1., a1, a2)
    }

    fn new_bandpass(mut frequency: f64, mut q: f64) -> Self {
        // No negative frequencies allowed.
        frequency = frequency.max(0.);

        // Don't let q go negative, which causes an unstable filter.
        q = 0_f64.max(q);

        if frequency <= 0. || frequency >= 1. {
            /* When the cutoff is zero, the z-transform approaches 0, if q
             * > 0. When both q and cutoff are zero, the z-transform is
             * pretty much undefined. What should we do in this case?
             * For now, just make the filter 0. When the cutoff is 1, the
             * z-transform also approaches 0.
             */
            return Self::new_coefficient(0., 0., 0., 1., 0., 0.);
        }
        if q <= 0. {
            /* When q = 0, the above formulas have problems. If we
             * look at the z-transform, we can see that the limit
             * as q->0 is 1, so set the filter that way.
             */
            return Self::new_coefficient(1., 0., 0., 1., 0., 0.);
        }

        let w0 = PI * frequency;
        let alpha = w0.sin() / (2. * q);
        let k = w0.cos();

        let b0 = alpha;
        let b1 = 0.;
        let b2 = -alpha;
        let a0 = 1. + alpha;
        let a1 = -2. * k;
        let a2 = 1. - alpha;

        Self::new_coefficient(b0, b1, b2, a0, a1, a2)
    }

    fn new_lowshelf(mut frequency: f64, db_gain: f64) -> Self {
        // Clip frequencies to between 0 and 1, inclusive.
        frequency = frequency.max(0.).min(1.);

        let a = 10_f64.powf(db_gain / 40.);

        if frequency == 1. {
            // The z-transform is a constant gain.
            return Self::new_coefficient(a * a, 0., 0., 1., 0., 0.);
        }
        if frequency <= 0. {
            // When frequency is 0, the z-transform is 1.
            return Self::new_coefficient(1., 0., 0., 1., 0., 0.);
        }

        let w0 = PI * frequency;
        let s = 1.; // filter slope (1 is max value)
        let alpha = 0.5 * w0.sin() * ((a + 1. / a) * (1. / s - 1.) + 2.).sqrt();
        let k = w0.cos();
        let k2 = 2. * a.sqrt() * alpha;
        let a_plus_one = a + 1.;
        let a_minus_one = a - 1.;

        let b0 = a * (a_plus_one - a_minus_one * k + k2);
        let b1 = 2. * a * (a_minus_one - a_plus_one * k);
        let b2 = a * (a_plus_one - a_minus_one * k - k2);
        let a0 = a_plus_one + a_minus_one * k + k2;
        let a1 = -2. * (a_minus_one + a_plus_one * k);
        let a2 = a_plus_one + a_minus_one * k - k2;

        Self::new_coefficient(b0, b1, b2, a0, a1, a2)
    }

    fn new_highshelf(mut frequency: f64, db_gain: f64) -> Self {
        // Clip frequencies to between 0 and 1, inclusive.
        frequency = frequency.max(0.).min(1.);

        let a = 10_f64.powf(db_gain / 40.);

        if frequency == 1. {
            // The z-transform is 1.
            return Self::new_coefficient(1., 0., 0., 1., 0., 0.);
        }
        if frequency <= 0. {
            // When frequency = 0, the filter is just a gain, a^2.
            return Self::new_coefficient(a * a, 0., 0., 1., 0., 0.);
        }

        let w0 = PI * frequency;
        let s = 1.; // filter slope (1 is max value)
        let alpha = 0.5 * w0.sin() * ((a + 1. / a) * (1. / s - 1.) + 2.).sqrt();
        let k = w0.cos();
        let k2 = 2. * a.sqrt() * alpha;
        let a_plus_one = a + 1.;
        let a_minus_one = a - 1.;

        let b0 = a * (a_plus_one + a_minus_one * k + k2);
        let b1 = -2. * a * (a_minus_one + a_plus_one * k);
        let b2 = a * (a_plus_one + a_minus_one * k - k2);
        let a0 = a_plus_one - a_minus_one * k + k2;
        let a1 = 2. * (a_minus_one - a_plus_one * k);
        let a2 = a_plus_one - a_minus_one * k - k2;

        Self::new_coefficient(b0, b1, b2, a0, a1, a2)
    }

    fn new_peaking(mut frequency: f64, mut q: f64, db_gain: f64) -> Self {
        // Clip frequencies to between 0 and 1, inclusive.
        frequency = frequency.max(0.).min(1.);

        // Don't let Q go negative, which causes an unstable filter.
        q = 0_f64.max(q);

        let a = 10_f64.powf(db_gain / 40.);

        if frequency <= 0. || frequency >= 1. {
            // When frequency is 0 or 1, the z-transform is 1.
            return Self::new_coefficient(1., 0., 0., 1., 0., 0.);
        }
        if q <= 0. {
            /* When q = 0, the above formulas have problems. If we
             * look at the z-transform, we can see that the limit
             * as q->0 is a^2, so set the filter that way.
             */
            return Self::new_coefficient(a * a, 0., 0., 1., 0., 0.);
        }

        let w0 = PI * frequency;
        let alpha = w0.sin() / (2. * q);
        let k = w0.cos();

        let b0 = 1. + alpha * a;
        let b1 = -2. * k;
        let b2 = 1. - alpha * a;
        let a0 = 1. + alpha / a;
        let a1 = -2. * k;
        let a2 = 1. - alpha / a;

        Self::new_coefficient(b0, b1, b2, a0, a1, a2)
    }

    fn new_notch(mut frequency: f64, mut q: f64) -> Self {
        // Clip frequencies to between 0 and 1, inclusive.
        frequency = frequency.max(0.).min(1.);

        // Don't let q go negative, which causes an unstable filter.
        q = q.max(0.);

        if frequency <= 0. || frequency >= 1. {
            // When frequency is 0 or 1, the z-transform is 1.
            return Self::new_coefficient(1., 0., 0., 1., 0., 0.);
        }
        if q <= 0. {
            /* When q = 0, the above formulas have problems. If we
             * look at the z-transform, we can see that the limit
             * as q->0 is 0, so set the filter that way.
             */
            return Self::new_coefficient(0., 0., 0., 1., 0., 0.);
        }

        let w0 = PI * frequency;
        let alpha = w0.sin() / (2. * q);
        let k = w0.cos();

        let b0 = 1.;
        let b1 = -2. * k;
        let b2 = 1.;
        let a0 = 1. + alpha;
        let a1 = -2. * k;
        let a2 = 1. - alpha;

        Self::new_coefficient(b0, b1, b2, a0, a1, a2)
    }

    fn new_allpass(mut frequency: f64, mut q: f64) -> Self {
        // Clip frequencies to between 0 and 1, inclusive.
        frequency = frequency.max(0.).min(1.);

        // Don't let q go negative, which causes an unstable filter.
        q = q.max(0.0);

        if frequency <= 0. || frequency >= 1. {
            // When frequency is 0 or 1, the z-transform is 1.
            return Self::new_coefficient(1., 0., 0., 1., 0., 0.);
        }

        if q <= 0. {
            /* When q = 0, the above formulas have problems. If we
             * look at the z-transform, we can see that the limit
             * as q->0 is -1, so set the filter that way.
             */
            return Self::new_coefficient(-1., 0., 0., 1., 0., 0.);
        }

        let w0 = PI * frequency;
        let alpha = w0.sin() / (2. * q);
        let k = w0.cos();

        let b0 = 1. - alpha;
        let b1 = -2. * k;
        let b2 = 1. + alpha;
        let a0 = 1. + alpha;
        let a1 = -2. * k;
        let a2 = 1. - alpha;

        Self::new_coefficient(b0, b1, b2, a0, a1, a2)
    }

    pub fn set(&mut self, enum_type: BiquadType, freq: f64, q: f64, gain: f64) {
        *self = match enum_type {
            BiquadType::BQ_LOWPASS => Self::new_lowpass(freq, q),
            BiquadType::BQ_HIGHPASS => Self::new_highpass(freq, q),
            BiquadType::BQ_BANDPASS => Self::new_bandpass(freq, q),
            BiquadType::BQ_LOWSHELF => Self::new_lowshelf(freq, gain),
            BiquadType::BQ_HIGHSHELF => Self::new_highshelf(freq, gain),
            BiquadType::BQ_PEAKING => Self::new_peaking(freq, q, gain),
            BiquadType::BQ_NOTCH => Self::new_notch(freq, q),
            BiquadType::BQ_ALLPASS => Self::new_allpass(freq, q),
            BiquadType::BQ_NONE => Self::new_coefficient(1., 0., 0., 1., 0., 0.),
        }
    }
}

#[cfg(test)]
mod tests {
    use crate::biquad::Biquad;
    use crate::biquad::BiquadType;

    fn assert_same_biquad(bq0: &Biquad, bq1: &Biquad) {
        assert_eq!(bq0.b0, bq1.b0);
        assert_eq!(bq0.b1, bq1.b1);
        assert_eq!(bq0.b2, bq1.b2);
        assert_eq!(bq0.a1, bq1.a1);
        assert_eq!(bq0.a2, bq1.a2);
    }

    #[test]
    fn invalid_frequency_test() {
        let mut bq = Biquad::new();
        let f_over: f32 = 1.5;
        let f_under: f32 = -0.1;
        let db_gain: f64 = 2.;
        let a: f64 = 10_f64.powf(db_gain / 40.);

        // check response to freq >= 1
        bq.set(BiquadType::BQ_LOWPASS, f_over as f64, 0., db_gain);
        let mut test_bq = Biquad::new();
        test_bq.b0 = 1.;
        assert_same_biquad(&bq, &test_bq);

        bq.set(BiquadType::BQ_HIGHPASS, f_over as f64, 0., db_gain);
        test_bq = Biquad::new();
        assert_same_biquad(&bq, &test_bq);

        bq.set(BiquadType::BQ_BANDPASS, f_over as f64, 0., db_gain);
        test_bq = Biquad::new();
        assert_same_biquad(&bq, &test_bq);

        bq.set(BiquadType::BQ_LOWSHELF, f_over as f64, 0., db_gain);
        test_bq = Biquad::new();
        test_bq.b0 = (a * a) as f32;
        assert_same_biquad(&bq, &test_bq);

        bq.set(BiquadType::BQ_HIGHSHELF, f_over as f64, 0., db_gain);
        test_bq = Biquad::new();
        test_bq.b0 = 1.;
        assert_same_biquad(&bq, &test_bq);

        bq.set(BiquadType::BQ_PEAKING, f_over as f64, 0., db_gain);
        test_bq = Biquad::new();
        test_bq.b0 = 1.;
        assert_same_biquad(&bq, &test_bq);

        bq.set(BiquadType::BQ_NOTCH, f_over as f64, 0., db_gain);
        test_bq = Biquad::new();
        test_bq.b0 = 1.;
        assert_same_biquad(&bq, &test_bq);

        bq.set(BiquadType::BQ_ALLPASS, f_over as f64, 0., db_gain);
        test_bq = Biquad::new();
        test_bq.b0 = 1.;
        assert_same_biquad(&bq, &test_bq);

        // check response to frew <= 0
        bq.set(BiquadType::BQ_LOWPASS, f_under as f64, 0., db_gain);
        let mut test_bq = Biquad::new();
        assert_same_biquad(&bq, &test_bq);

        bq.set(BiquadType::BQ_HIGHPASS, f_under as f64, 0., db_gain);
        test_bq = Biquad::new();
        test_bq.b0 = 1.;
        assert_same_biquad(&bq, &test_bq);

        bq.set(BiquadType::BQ_BANDPASS, f_under as f64, 0., db_gain);
        test_bq = Biquad::new();
        assert_same_biquad(&bq, &test_bq);

        bq.set(BiquadType::BQ_LOWSHELF, f_under as f64, 0., db_gain);
        test_bq = Biquad::new();
        test_bq.b0 = 1.;
        assert_same_biquad(&bq, &test_bq);

        bq.set(BiquadType::BQ_HIGHSHELF, f_under as f64, 0., db_gain);
        test_bq = Biquad::new();
        test_bq.b0 = (a * a) as f32;
        assert_same_biquad(&bq, &test_bq);

        bq.set(BiquadType::BQ_PEAKING, f_under as f64, 0., db_gain);
        test_bq = Biquad::new();
        test_bq.b0 = 1.;
        assert_same_biquad(&bq, &test_bq);

        bq.set(BiquadType::BQ_NOTCH, f_under as f64, 0., db_gain);
        test_bq = Biquad::new();
        test_bq.b0 = 1.;
        assert_same_biquad(&bq, &test_bq);

        bq.set(BiquadType::BQ_ALLPASS, f_under as f64, 0., db_gain);
        test_bq = Biquad::new();
        test_bq.b0 = 1.;
        assert_same_biquad(&bq, &test_bq);
    }

    #[test]
    fn invalid_q_test() {
        let mut bq = Biquad::new();
        let f: f32 = 0.5;
        let q: f32 = -0.1;
        let db_gain: f64 = 2.;
        let a: f64 = 10_f64.powf(db_gain / 40.);

        // check response to Q <= 0
        // Low and High pass filters scope Q making the test mute

        bq.set(BiquadType::BQ_BANDPASS, f as f64, q as f64, db_gain);
        let mut test_bq = Biquad::new();
        test_bq.b0 = 1.;
        assert_same_biquad(&bq, &test_bq);

        // Low and high shelf do not compute resonance

        bq.set(BiquadType::BQ_PEAKING, f as f64, q as f64, db_gain);
        test_bq = Biquad::new();
        test_bq.b0 = (a * a) as f32;
        assert_same_biquad(&bq, &test_bq);

        bq.set(BiquadType::BQ_NOTCH, f as f64, q as f64, db_gain);
        test_bq = Biquad::new();
        assert_same_biquad(&bq, &test_bq);

        bq.set(BiquadType::BQ_ALLPASS, f as f64, q as f64, db_gain);
        test_bq = Biquad::new();
        test_bq.b0 = -1.;
        assert_same_biquad(&bq, &test_bq);
    }
}
