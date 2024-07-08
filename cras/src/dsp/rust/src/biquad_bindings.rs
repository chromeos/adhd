// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::biquad::Biquad;
use crate::biquad::BiquadType;

/* Return a biquad structure pointer with filter parameters initialized
 * from its type and parameters.
 * Args:
 *    type - The type of the biquad filter.
 *    frequency - The value should be in the range [0, 1]. It is relative to
 *        half of the sampling rate.
 *    Q - Quality factor. See Web Audio API for details.
 *    gain - The value is in dB. See Web Audio API for details.
 */
#[no_mangle]
pub extern "C" fn biquad_new_set(enum_type: BiquadType, freq: f64, q: f64, gain: f64) -> Biquad {
    Biquad::new_set(enum_type, freq, q, gain)
}
