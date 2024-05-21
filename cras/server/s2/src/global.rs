// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use std::ffi::c_char;
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
pub extern "C" fn cras_s2_get_style_transfer_allowed() -> bool {
    state().output.style_transfer_allowed
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
pub extern "C" fn cras_s2_load_cras_config_dir() {
    match std::fs::read_to_string("/run/chromeos-config/v1/audio/main/cras-config-dir") {
        Ok(str) => {
            state().set_cras_config_dir(&str);
        }
        Err(err) => {
            state().set_cras_config_dir("");
            log::info!("Failed to read cras-config-dir: {err}");
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
