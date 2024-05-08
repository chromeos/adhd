// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use std::ffi::c_char;
use std::ffi::CStr;
use std::ffi::CString;
use std::ops::Deref;
use std::sync::Mutex;
use std::sync::MutexGuard;
use std::sync::OnceLock;

fn state() -> MutexGuard<'static, crate::S2> {
    static CELL: OnceLock<Mutex<crate::S2>> = OnceLock::new();
    CELL.get_or_init(|| Mutex::new(crate::S2::new()))
        .lock()
        .unwrap()
}

#[no_mangle]
pub extern "C" fn cras_s2_set_ap_nc_featured_allowed(allowed: bool) {
    state().set_ap_nc_featured_allowed(allowed);
}

#[no_mangle]
pub extern "C" fn cras_s2_set_ap_nc_segmentation_allowed(allowed: bool) {
    state().set_ap_nc_segmentation_allowed(allowed);
}

#[no_mangle]
pub extern "C" fn cras_s2_set_ap_nc_feature_tier_allowed(allowed: bool) {
    state().set_ap_nc_feature_tier_allowed(allowed);
}

pub fn set_dlc_manager_ready() {
    state().set_dlc_manager_ready();
}

#[no_mangle]
pub extern "C" fn cras_s2_get_ap_nc_allowed() -> bool {
    state().output.ap_nc_allowed
}

#[no_mangle]
pub extern "C" fn cras_s2_set_style_transfer_featured_allowed(allowed: bool) {
    state().set_style_transfer_featured_allowed(allowed);
}

#[no_mangle]
pub extern "C" fn cras_s2_get_style_transfer_supported() -> bool {
    state().output.style_transfer_supported
}

#[no_mangle]
pub extern "C" fn cras_s2_set_style_transfer_enabled(enabled: bool) {
    state().set_style_transfer_enabled(enabled);
}

#[no_mangle]
pub extern "C" fn cras_s2_get_style_transfer_enabled() -> bool {
    state().output.style_transfer_enabled
}

#[no_mangle]
pub unsafe extern "C" fn cras_s2_set_ucm_suffix(ucm_suffix: *const libc::c_char) {
    if ucm_suffix.is_null() {
        state().set_ucm_suffix("");
    } else {
        match CStr::from_ptr(ucm_suffix).to_str() {
            Ok(str) => {
                state().set_ucm_suffix(str);
            }
            Err(err) => {
                state().set_ucm_suffix("");
                log::error!("ucm_suffix is not a valid string: {err}");
            }
        }
    }
}

#[no_mangle]
pub extern "C" fn cras_s2_get_beamforming_supported() -> bool {
    state().output.beamforming_supported
}

#[no_mangle]
pub extern "C" fn cras_s2_dump_json() -> *mut c_char {
    let s = serde_json::to_string_pretty(state().deref()).expect("serde_json::to_string_pretty");
    CString::new(s).expect("CString::new").into_raw()
}
