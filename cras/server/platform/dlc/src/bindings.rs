// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use std::ffi::c_char;
use std::ffi::CStr;
use std::ffi::CString;
use std::ptr;

use super::get_dlc_state;
use super::install_dlc;
use super::CrasDlcId;
use super::Result;

fn get_dlc_root_path(id: CrasDlcId) -> Result<CString> {
    let dlc_state = get_dlc_state(id)?;
    CString::new(dlc_state.root_path).map_err(|e| e.into())
}

/// Returns `true` if the installation request is successfully sent,
/// otherwise returns `false`.
#[no_mangle]
pub extern "C" fn cras_dlc_install(id: CrasDlcId) -> bool {
    match install_dlc(id) {
        Ok(()) => true,
        Err(err) => {
            log::warn!("cras_dlc_install({}) failed: {}", id, err);
            false
        }
    }
}

/// Returns `true` if the DLC package is ready for use, otherwise
/// returns `false`.
#[no_mangle]
pub extern "C" fn cras_dlc_is_available(id: CrasDlcId) -> bool {
    match get_dlc_state(id) {
        Ok(dlc_state) => dlc_state.installed,
        Err(_) => false,
    }
}

/// Returns the root path of the DLC package.
/// The returned string should be freed with cras_rust_free_string.
#[no_mangle]
pub extern "C" fn cras_dlc_get_root_path(id: CrasDlcId) -> *mut c_char {
    match get_dlc_root_path(id) {
        Ok(root_path) => root_path.into_raw(),
        Err(_) => ptr::null_mut(),
    }
}

/// Writes the DLC ID string corresponding to the enum id to `ret`.
/// Suggested value of `ret_len` is `CRAS_DLC_ID_STRING_MAX_LENGTH`.
///
/// # Safety
/// `ret` should have `ret_len` bytes writable.
#[no_mangle]
pub unsafe extern "C" fn cras_dlc_get_id_string(ret: *mut c_char, ret_len: usize, id: CrasDlcId) {
    let len = std::cmp::min(id.as_str().as_bytes().len(), ret_len - 1);
    std::ptr::copy(id.as_str().as_bytes().as_ptr().cast(), ret, len);
    std::ptr::write(ret.add(len) as *mut u8, 0u8);
}

/// Overrides the DLC state for DLC `id`.
///
/// # Safety
/// root_path must be a valid NULL terminated UTF-8 string.
#[no_mangle]
pub unsafe extern "C" fn cras_dlc_override_state_for_testing(
    id: CrasDlcId,
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
        id,
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
