// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use std::f64::consts::FRAC_2_PI;
use std::f64::consts::FRAC_PI_2;

use num_traits::Float;

pub const NEG_TWO_DB: f64 = 0.7943282347242815; // -2dB = 10^(-2/20)

pub fn db_to_linear_table() -> [f32; 201] {
    let mut arr: [f32; 201] = [0.; 201];
    for (i, j) in (-100..=100).enumerate() {
        arr[i] = 10_f64.powf((j as f64) / 20.) as f32;
    }
    arr
}

#[allow(non_upper_case_globals)]
pub fn db_to_linear(db: usize) -> f32 {
    thread_local! {
        static table: [f32; 201] = db_to_linear_table();
    }
    table.with(|x| x[db])
}

pub fn isbadf(x: f32) -> bool {
    let bits: u32 = x.to_bits();
    let exp = (bits >> 23) & 0xff;
    exp == 0xff
}

#[allow(dead_code)]
pub mod slow {
    use super::Float;
    use super::FRAC_2_PI;
    use super::FRAC_PI_2;

    pub fn decibels_to_linear(decibels: f32) -> f32 {
        // 10^(x/20) = e^(x * log(10^(1/20)))
        (0.1151292546497022 * decibels).exp()
    }

    pub fn frexpf(x: f32, e: &mut i32) -> f32 {
        if x == 0. {
            *e = 0;
            return 0.;
        }
        let (man, exp, sign) = Float::integer_decode(x);
        *e = (man as f32).log2().floor() as i32 + exp as i32 + 1;
        sign as f32 * 2_f32.powf(((man as f64).log2().fract() - 1.) as f32)
    }

    pub fn linear_to_decibels(linear: f32) -> f32 {
        if linear <= 0. {
            return -1000.;
        }
        // 20 * log10(x) = 20 / log(10) * log(x)
        8.6858896380650366 * linear.ln()
    }

    pub fn warp_sinf(x: f32) -> f32 {
        ((FRAC_PI_2 as f32) * x).sin()
    }

    pub fn warp_asinf(x: f32) -> f32 {
        x.asin() * (FRAC_2_PI as f32)
    }

    pub fn knee_expf(input: f32) -> f32 {
        input.exp()
    }
}

#[allow(non_snake_case)]
pub mod fast {
    use super::db_to_linear;
    use super::FRAC_2_PI;

    pub fn decibels_to_linear(decibels: f32) -> f32 {
        let fi: f32 = decibels.round();
        let x: f32 = decibels - fi;
        let i: i32 = (fi as i32).min(100).max(-100);

        /* Coefficients obtained from:
         * fpminimax(10^(x/20), [|1,2,3|], [|SG...|], [-0.5;0.5], 1, absolute);
         * max error ~= 7.897e-8
         */
        let A3: f32 = 2.54408805631101131439208984375e-4;
        let A2: f32 = 6.628888659179210662841796875e-3;
        let A1: f32 = 0.11512924730777740478515625;
        let A0: f32 = 1.;

        let x2: f32 = x * x;
        ((A3 * x + A2) * x2 + (A1 * x + A0)) * db_to_linear((i + 100) as usize)
    }

    pub fn frexpf(x: f32, e: &mut i32) -> f32 {
        if x == 0. {
            *e = 0;
            return 0.;
        }
        let bits: u32 = x.to_bits();
        let neg: u32 = bits >> 31;
        let exp: i32 = ((bits >> 23) & 0xff) as i32;
        let man: u32 = bits & 0x7fffff;
        *e = (exp - 126) as i32;
        f32::from_bits(neg << 31 | 126 << 23 | man)
    }

    pub fn linear_to_decibels(linear: f32) -> f32 {
        if linear <= 0. {
            return -1000.;
        }
        let mut e: i32 = 0;
        let mut x: f32 = frexpf(linear, &mut e);
        let mut exp: f32 = e as f32;

        if x > 0.707106781186548 {
            x *= 0.707106781186548;
            exp += 0.5;
        }

        /* Coefficients obtained from:
         * fpminimax(log10(x), 5, [|SG...|], [1/2;sqrt(2)/2], absolute);
         * max err ~= 6.088e-8
         */
        let A5: f32 = 1.131880283355712890625;
        let A4: f32 = -4.258677959442138671875;
        let A3: f32 = 6.81631565093994140625;
        let A2: f32 = -6.1185703277587890625;
        let A1: f32 = 3.6505267620086669921875;
        let A0: f32 = -1.217894077301025390625;

        let x2: f32 = x * x;
        let x4: f32 = x2 * x2;
        ((A5 * x + A4) * x4 + (A3 * x + A2) * x2 + (A1 * x + A0)) * 20. + exp * 6.0205999132796239
    }

    pub fn warp_sinf(x: f32) -> f32 {
        let A7: f32 = -4.3330336920917034149169921875e-3;
        let A5: f32 = 7.9434238374233245849609375e-2;
        let A3: f32 = -0.645892798900604248046875;
        let A1: f32 = 1.5707910060882568359375;

        let x2: f32 = x * x;
        let x4: f32 = x2 * x2;
        x * ((A7 * x2 + A5) * x4 + (A3 * x2 + A1))
    }

    pub fn warp_asinf(x: f32) -> f32 {
        x.asin() * (FRAC_2_PI as f32)
    }

    pub fn knee_expf(input: f32) -> f32 {
        // exp(x) = decibels_to_linear(20*log10(e)*x)
        decibels_to_linear(8.685889638065044 * input)
    }
}

pub use fast::*;

#[cfg(test)]
mod tests {
    use float_cmp::assert_approx_eq;

    use crate::drc_math::fast;
    use crate::drc_math::slow;

    // The functions in the slow module are built with library functions, and
    // the functions in the fast module are approximating functions we
    // implemented. We compare the output of the same functions in fast module
    // and slow module to verify the correctness of functions in fast module.

    #[test]
    fn decibels_to_linear_test() {
        assert_approx_eq!(
            f32,
            fast::decibels_to_linear(0.),
            slow::decibels_to_linear(0.)
        );
        assert_approx_eq!(
            f32,
            fast::decibels_to_linear(5.),
            slow::decibels_to_linear(5.)
        );
        assert_approx_eq!(
            f32,
            fast::decibels_to_linear(10.),
            slow::decibels_to_linear(10.)
        );
        assert_approx_eq!(
            f32,
            fast::decibels_to_linear(15.),
            slow::decibels_to_linear(15.)
        );
        assert_approx_eq!(
            f32,
            fast::decibels_to_linear(20.),
            slow::decibels_to_linear(20.)
        );
    }

    #[test]
    fn frexpf_test() {
        let mut e1: i32 = 0;
        let mut e2: i32 = 0;
        assert_approx_eq!(f32, fast::frexpf(0., &mut e1), slow::frexpf(0., &mut e2));
        assert_eq!(e1, e2);
        assert_approx_eq!(
            f32,
            fast::frexpf(0.05, &mut e1),
            slow::frexpf(0.05, &mut e2)
        );
        assert_eq!(e1, e2);
        assert_approx_eq!(
            f32,
            fast::frexpf(-0.05, &mut e1),
            slow::frexpf(-0.05, &mut e2)
        );
        assert_eq!(e1, e2);
        assert_approx_eq!(
            f32,
            fast::frexpf(500., &mut e1),
            slow::frexpf(500., &mut e2)
        );
        assert_eq!(e1, e2);
        assert_approx_eq!(
            f32,
            fast::frexpf(10000., &mut e1),
            slow::frexpf(10000., &mut e2)
        );
        assert_eq!(e1, e2);
    }

    #[test]
    fn linear_to_decibels_test() {
        assert_approx_eq!(
            f32,
            fast::linear_to_decibels(0.),
            slow::linear_to_decibels(0.)
        );
        assert_approx_eq!(
            f32,
            fast::linear_to_decibels(0.05),
            slow::linear_to_decibels(0.05)
        );
        assert_approx_eq!(
            f32,
            fast::linear_to_decibels(-0.05),
            slow::linear_to_decibels(-0.05)
        );
        assert_approx_eq!(
            f32,
            fast::linear_to_decibels(500.),
            slow::linear_to_decibels(500.)
        );
        assert_approx_eq!(
            f32,
            fast::linear_to_decibels(10000.),
            slow::linear_to_decibels(10000.)
        );
    }

    #[test]
    fn warp_sinf_test() {
        assert_approx_eq!(
            f32,
            fast::warp_sinf(0.),
            slow::warp_sinf(0.),
            epsilon = 0.00001
        );
        assert_approx_eq!(
            f32,
            fast::warp_sinf(0.2),
            slow::warp_sinf(0.2),
            epsilon = 0.00001
        );
        assert_approx_eq!(
            f32,
            fast::warp_sinf(0.4),
            slow::warp_sinf(0.4),
            epsilon = 0.00001
        );
        assert_approx_eq!(
            f32,
            fast::warp_sinf(0.6),
            slow::warp_sinf(0.6),
            epsilon = 0.00001
        );
        assert_approx_eq!(
            f32,
            fast::warp_sinf(0.8),
            slow::warp_sinf(0.8),
            epsilon = 0.00001
        );
    }

    #[test]
    fn knee_expf_test() {
        assert_approx_eq!(f32, fast::knee_expf(0.), slow::knee_expf(0.));
        assert_approx_eq!(f32, fast::knee_expf(1.), slow::knee_expf(1.));
        assert_approx_eq!(f32, fast::knee_expf(0.5), slow::knee_expf(0.5));
        assert_approx_eq!(f32, fast::knee_expf(-2.), slow::knee_expf(-2.));
        assert_approx_eq!(f32, fast::knee_expf(5.), slow::knee_expf(5.));
    }
}
