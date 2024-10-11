// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use std::ffi::c_char;
use std::ffi::CString;

/// Free a string allocated from CRAS Rust functions.
///
/// # Safety
///
/// `s` must be a string allocated from CRAS Rust functions that asked for it to be freed
/// with this function.
#[no_mangle]
pub unsafe extern "C" fn cras_rust_free_string(s: *mut c_char) {
    if s.is_null() {
        return;
    }
    drop(CString::from_raw(s));
}

#[cfg(test)]
mod tests {
    use std::ffi::CString;

    #[test]
    fn free() {
        let s = CString::new("foo").unwrap().into_raw();
        unsafe { super::cras_rust_free_string(s) };
    }

    #[test]
    fn free_null() {
        unsafe { super::cras_rust_free_string(std::ptr::null_mut()) };
    }
}
