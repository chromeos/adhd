// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

extern crate libc;
use std::option::Option;

const RAMP_TIME_MS: f32 = 20.0;

#[derive(Debug, Default, PartialEq)]
pub struct DCBlock {
    r: f32,
    x_prev: Option<f32>,
    y_prev: f32,
    ramp_factor: f32,
    ramp_increment: f32,
}

impl DCBlock {
    pub fn new() -> Self {
        DCBlock {
            r: 0.0,
            x_prev: None,
            y_prev: 0.0,
            ramp_factor: 0.0,
            ramp_increment: 0.0,
        }
    }

    pub fn set_config(&mut self, r: f32, sample_rate: u64) {
        self.r = r;
        self.ramp_increment = 1000.0 / (RAMP_TIME_MS * (sample_rate as f32));
    }

    pub fn process(&mut self, data: &mut [f32]) {
        let mut x_prev: f32 = match self.x_prev {
            Some(x) => x,
            None => data[0],
        };
        let mut y_prev: f32 = self.y_prev;
        let r: f32 = self.r;

        for x in data {
            let mut d: f32 = *x - x_prev + r * y_prev;

            y_prev = d;
            x_prev = *x;

            /*
             * It takes a while for this DC-block filter to completely
             * filter out a large DC-offset, so apply a mix-in ramp to
             * avoid any residual jump discontinuities that can lead to
             * "pop" during capture.
             */

            if self.ramp_factor < 1.0 {
                d *= self.ramp_factor;
                self.ramp_factor += self.ramp_increment;
            }
            *x = d;
        }
        self.x_prev = Some(x_prev);
        self.y_prev = y_prev;
    }
}

#[cfg(test)]
mod tests {
    use crate::dcblock::DCBlock;

    #[test]
    fn dcblock_test() {
        let mut dcblock = DCBlock::new();
        assert_eq!(
            dcblock,
            DCBlock {
                r: 0.0,
                x_prev: None,
                y_prev: 0.0,
                ramp_factor: 0.0,
                ramp_increment: 0.0,
            }
        );
        dcblock.set_config(0.995, 50000);
        assert_eq!(
            dcblock,
            DCBlock {
                r: 0.995,
                x_prev: None,
                y_prev: 0.0,
                ramp_factor: 0.0,
                ramp_increment: 0.001,
            }
        );
        let mut data: [f32; 5] = [5.0, 10.0, 15.0, 20.0, 25.0];
        dcblock.process(&mut data);

        // The following expected outputs are generated from python script

        let eps: f32 = 0.000002;
        let rdata: [f32; 5] = [0.0, 0.005, 0.01995, 0.044775375, 0.0794019975];
        let rdata2: [f32; 5] = [
            -0.026243765609375008,
            -0.03130505613759376,
            -0.03623530266639009,
            -0.04100611260349502,
            -0.045589577693037245,
        ];
        for (data_i, rdata_i) in std::iter::zip(data, rdata) {
            assert!((data_i - rdata_i).abs() < eps);
        }
        assert!((dcblock.ramp_factor - 0.005).abs() < eps);
        match dcblock.x_prev {
            Some(dcblock_x_prev) => assert!((dcblock_x_prev - 25_f32).abs() < eps),
            None => panic!("Value Mismatch!"),
        };
        assert!((dcblock.y_prev - 19.850499375).abs() < eps);
        dcblock.process(&mut data);
        for (data_i, rdata2_i) in std::iter::zip(data, rdata2) {
            assert!((data_i - rdata2_i).abs() < eps);
        }
        assert!((dcblock.ramp_factor - 0.010000000000000002).abs() < eps);
        match dcblock.x_prev {
            Some(dcblock_x_prev) => assert!((dcblock_x_prev - 0.0794019975).abs() < eps),
            None => panic!("Value Mismatch!"),
        };
        assert!((dcblock.y_prev + 5.065508632559693).abs() < eps);
    }
}
