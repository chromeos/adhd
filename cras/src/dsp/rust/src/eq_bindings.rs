// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::biquad::Biquad;
use crate::biquad::BiquadType;
use crate::eq::EQ;

#[no_mangle]
// Create an EQ.
pub extern "C" fn eq_new() -> *mut EQ {
    Box::into_raw(Box::new(EQ::new()))
}

#[no_mangle]
// Free an EQ.
pub unsafe extern "C" fn eq_free(eq: *mut EQ) {
    if let Some(eq) = eq.as_mut() {
        drop(Box::from_raw(eq));
    }
}

#[no_mangle]
/* Append a biquad filter to an EQ. An EQ can have at most MAX_BIQUADS_PER_EQ
 * biquad filters.
 * Args:
 *    eq - The EQ we want to use.
 *    type - The type of the biquad filter we want to append.
 *    frequency - The value should be in the range [0, 1]. It is relative to
 *        half of the sampling rate.
 *    Q, gain - The meaning depends on the type of the filter. See Web Audio
 *        API for details.
 * Returns:
 *    0 if success. -1 if the eq has no room for more biquads.
 */
pub unsafe extern "C" fn eq_append_biquad(
    eq: *mut EQ,
    enum_type: BiquadType,
    freq: f32,
    q: f32,
    gain: f32,
) -> i32 {
    if let Some(eq) = eq.as_mut() {
        return match eq.append_biquad(enum_type, freq as f64, q as f64, gain as f64) {
            Ok(_) => 0,
            Err(errno) => -(errno as i32),
        };
    }
    -1
}

#[no_mangle]
/* Append a biquad filter to an EQ. An EQ can have at most MAX_BIQUADS_PER_EQ
 * biquad filters. This is similar to eq_append_biquad(), but it specifies the
 * biquad coefficients directly.
 * Args:
 *    eq - The EQ we want to use.
 *    biquad - The parameters for the biquad filter.
 * Returns:
 *    0 if success. -1 if the eq has no room for more biquads.
 */
pub unsafe extern "C" fn eq_append_biquad_direct(eq: *mut EQ, biquad: &Biquad) -> i32 {
    if let Some(eq) = eq.as_mut() {
        return match eq.append_biquad_direct(biquad) {
            Ok(_) => 0,
            Err(errno) => -(errno as i32),
        };
    }
    -1
}

#[no_mangle]
/* Process a buffer of audio data through the EQ.
 * Args:
 *    eq - The EQ we want to use.
 *    data - The array of audio samples.
 *    count - The number of elements in the data array to process.
 */
pub unsafe extern "C" fn eq_process(eq: *mut EQ, data: *mut f32, count: i32) {
    if count == 0 {
        return;
    }
    if let Some(eq) = eq.as_mut() {
        eq.process(unsafe { std::slice::from_raw_parts_mut(data, count as usize) });
    }
}
