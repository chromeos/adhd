// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
use crate::crossover::Crossover;

#[no_mangle]
/// Initializes a crossover filter
/// Args:
///    xo - The crossover filter we want to initialize.
///    freq1 - The normalized frequency splits low and mid band.
///    freq2 - The normalized frequency splits mid and high band.
pub unsafe extern "C" fn crossover_init(xo: *mut Crossover, freq1: f32, freq2: f32) {
    if let Some(xo) = xo.as_mut() {
        *xo = Crossover::new(freq1, freq2);
    }
}

#[no_mangle]
/// Splits input samples to three bands.
/// Args:
///    xo - The crossover filter to use.
///    count - The number of input samples.
///    data0 - The input samples, also the place to store low band output.
///    data1 - The place to store mid band output.
///    data2 - The place to store high band output.
pub unsafe extern "C" fn crossover_process(
    xo: *mut Crossover,
    count: i32,
    data0: *mut f32,
    data1: *mut f32,
    data2: *mut f32,
) {
    if count == 0 {
        return;
    }
    if let Some(xo) = xo.as_mut() {
        xo.process(
            unsafe { std::slice::from_raw_parts_mut(data0, count as usize) },
            unsafe { std::slice::from_raw_parts_mut(data1, count as usize) },
            unsafe { std::slice::from_raw_parts_mut(data2, count as usize) },
        );
    }
}
