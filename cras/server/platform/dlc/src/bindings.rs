// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use std::ffi::c_char;
use std::ffi::CStr;
use std::ffi::CString;
use std::ptr;
use std::thread;

use cras_common::types_internal::SR_BT_DLC;

use super::get_dlc_state_cached;
use super::Result;
use crate::download_dlcs_until_installed;
use crate::DlcInstallOnFailureCallback;
use crate::DlcInstallOnSuccessCallback;

fn get_dlc_root_path(id: &str) -> Result<CString> {
    let dlc_state = get_dlc_state_cached(id);
    CString::new(dlc_state.root_path).map_err(|e| e.into())
}

/// Returns `true` if sr-bt-dlc is available.
#[no_mangle]
pub extern "C" fn cras_dlc_is_sr_bt_available() -> bool {
    get_dlc_state_cached(SR_BT_DLC).installed
}

/// Returns the root path of sr-bt-dlc.
/// The returned string should be freed with cras_rust_free_string.
#[no_mangle]
pub extern "C" fn cras_dlc_get_sr_bt_root_path() -> *mut c_char {
    match get_dlc_root_path(SR_BT_DLC) {
        Ok(root_path) => root_path.into_raw(),
        Err(_) => ptr::null_mut(),
    }
}

/// Overrides the DLC state for DLC `id`.
///
/// # Safety
/// root_path must be a valid NULL terminated UTF-8 string.
#[no_mangle]
pub unsafe extern "C" fn cras_dlc_override_sr_bt_for_testing(
    installed: bool,
    root_path: *const c_char,
) {
    let root_path = if root_path.is_null() {
        String::new()
    } else {
        CStr::from_ptr(root_path)
            .to_str()
            .expect("to_str() failed")
            .into()
    };
    crate::override_state_for_testing(
        SR_BT_DLC,
        crate::State {
            installed,
            root_path,
        },
    );
}

/// Reset all DLC overrides.
#[no_mangle]
pub extern "C" fn cras_dlc_reset_overrides_for_testing() {
    crate::reset_overrides();
}

/// Start a thread to download all DLCs.
#[no_mangle]
pub extern "C" fn download_dlcs_until_installed_with_thread(
    dlc_install_on_success_callback: DlcInstallOnSuccessCallback,
    dlc_install_on_failure_callback: DlcInstallOnFailureCallback,
) {
    thread::Builder::new()
        .name("cras-dlc".into())
        .spawn(move || {
            download_dlcs_until_installed(
                dlc_install_on_success_callback,
                dlc_install_on_failure_callback,
            )
        })
        .unwrap();
}
