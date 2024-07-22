// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::biquad::Biquad;
use crate::biquad::BiquadType;
use crate::eq2::EQ2;

#[no_mangle]
/// Create an EQ2.
pub extern "C" fn eq2_new() -> *mut EQ2 {
    Box::into_raw(Box::new(EQ2::new()))
}

#[no_mangle]
/// Free an EQ.
pub unsafe extern "C" fn eq2_free(eq2: *mut EQ2) {
    if let Some(eq2) = eq2.as_mut() {
        drop(Box::from_raw(eq2));
    }
}

#[no_mangle]
/// Append a biquad filter to an EQ2. An EQ2 can have at most MAX_BIQUADS_PER_EQ2
/// biquad filters per channel.
/// Args:
///    eq2 - The EQ2 we want to use.
///    channel - 0 or 1. The channel we want to append the filter to.
///    type - The type of the biquad filter we want to append.
///    frequency - The value should be in the range [0, 1]. It is relative to
///        half of the sampling rate.
///    Q, gain - The meaning depends on the type of the filter. See Web Audio
///        API for details.
/// Returns:
///    0 if success. -1 if the eq has no room for more biquads.
///
pub unsafe extern "C" fn eq2_append_biquad(
    eq2: *mut EQ2,
    channel: i32,
    enum_type: BiquadType,
    freq: f32,
    q: f32,
    gain: f32,
) -> i32 {
    if let Some(eq2) = eq2.as_mut() {
        return match eq2.append_biquad(
            channel as usize,
            enum_type,
            freq as f64,
            q as f64,
            gain as f64,
        ) {
            Ok(_) => 0,
            Err(errno) => -(errno as i32),
        };
    }
    -1
}

#[no_mangle]
/// Append a biquad filter to an EQ2. An EQ2 can have at most MAX_BIQUADS_PER_EQ2
/// biquad filters. This is similar to eq2_append_biquad(), but it specifies the
/// biquad coefficients directly.
/// Args:
///    eq2 - The EQ2 we want to use.
///    channel - 0 or 1. The channel we want to append the filter to.
///    biquad - The parameters for the biquad filter.
/// Returns:
///    0 if success. -1 if the eq has no room for more biquads.
///
pub unsafe extern "C" fn eq2_append_biquad_direct(
    eq2: *mut EQ2,
    channel: i32,
    biquad: Biquad,
) -> i32 {
    if let Some(eq2) = eq2.as_mut() {
        return match eq2.append_biquad_direct(channel as usize, biquad) {
            Ok(_) => 0,
            Err(errno) => -(errno as i32),
        };
    }
    -1
}

#[no_mangle]
/// Process a buffer of audio data through the EQ2.
/// Args:
///    eq2 - The EQ2 we want to use.
///    data0 - The array of channel 0 audio samples.
///    data1 - The array of channel 1 audio samples.
///    count - The number of elements in each of the data array to process.
///
pub unsafe extern "C" fn eq2_process(eq2: *mut EQ2, data0: *mut f32, data1: *mut f32, count: i32) {
    if count == 0 {
        return;
    }
    if let Some(eq2) = eq2.as_mut() {
        eq2.process(
            unsafe { std::slice::from_raw_parts_mut(data0, count as usize) },
            unsafe { std::slice::from_raw_parts_mut(data1, count as usize) },
        );
    }
}

#[no_mangle]
/// Get the number of biquads in the EQ2 channel.
pub unsafe extern "C" fn eq2_len(eq2: *mut EQ2, channel: i32) -> i32 {
    if let Some(eq2) = eq2.as_mut() {
        return eq2.n[channel as usize] as i32;
    }
    -1
}

#[no_mangle]
/// Get the biquad specified by index from the EQ2 channell
pub unsafe extern "C" fn eq2_get_bq(eq2: *mut EQ2, channel: i32, index: i32) -> *mut Biquad {
    &mut ((*eq2).biquads[index as usize][channel as usize])
}
