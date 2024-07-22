// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::crossover2::Crossover2;
/// "crossover2" is a two channel version of the "crossover" filter. It processes
/// two channels of data at once to increase performance.

#[no_mangle]
/// Initializes a crossover2 filter
/// Args:
///    xo2 - The crossover2 filter we want to initialize.
///    freq1 - The normalized frequency splits low and mid band.
///    freq2 - The normalized frequency splits mid and high band.
///
pub unsafe extern "C" fn crossover2_init(xo2: *mut Crossover2, freq1: f32, freq2: f32) {
    if let Some(xo2) = xo2.as_mut() {
        xo2.init(freq1 as f64, freq2 as f64);
    }
}

#[no_mangle]
/// Splits input samples to three bands.
/// Args:
///    xo2 - The crossover2 filter to use.
///    count - The number of input samples.
///    data0L, data0R - The input samples, also the place to store low band
///                     output.
///    data1L, data1R - The place to store mid band output.
///    data2L, data2R - The place to store high band output.
///
pub unsafe extern "C" fn crossover2_process(
    xo2: *mut Crossover2,
    count: i32,
    data0L: *mut f32,
    data0R: *mut f32,
    data1L: *mut f32,
    data1R: *mut f32,
    data2L: *mut f32,
    data2R: *mut f32,
) {
    if count == 0 {
        return;
    }
    if let Some(xo2) = xo2.as_mut() {
        (*xo2).process(
            unsafe { std::slice::from_raw_parts_mut(data0L, count as usize) },
            unsafe { std::slice::from_raw_parts_mut(data0R, count as usize) },
            unsafe { std::slice::from_raw_parts_mut(data1L, count as usize) },
            unsafe { std::slice::from_raw_parts_mut(data1R, count as usize) },
            unsafe { std::slice::from_raw_parts_mut(data2L, count as usize) },
            unsafe { std::slice::from_raw_parts_mut(data2R, count as usize) },
        );
    }
}
