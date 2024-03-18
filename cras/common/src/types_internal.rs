// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use std::ffi::CString;

use bitflags::bitflags;
use itertools::Itertools;

bitflags! {
    #[allow(non_camel_case_types)]
    #[repr(transparent)]
    pub struct CRAS_STREAM_ACTIVE_EFFECT: u64 {
        const ECHO_CANCELLATION = 1 << 0;
        const NOISE_SUPPRESSION = 1 << 1;
        const VOICE_ACTIVITY_DETECTION = 1 << 2;
        const NEGATE = 1 << 3;
        const NOISE_CANCELLATION = 1 << 4;
        const STYLE_TRANSFER = 1 << 5;
        const PROCESSOR_OVERRIDDEN = 1 << 6;
    }
}

/// Returns the names of active effects as a string.
/// The resulting string should be freed with cras_rust_free_string.
#[no_mangle]
pub extern "C" fn cras_stream_active_effects_string(
    effect: CRAS_STREAM_ACTIVE_EFFECT,
) -> *mut libc::c_char {
    if effect.is_empty() {
        CString::new("none").unwrap().into_raw()
    } else {
        let names = effect.iter_names().map(|(s, _)| s.to_lowercase()).join(" ");
        CString::new(names).unwrap().into_raw()
    }
}
