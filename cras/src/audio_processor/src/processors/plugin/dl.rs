// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use std::ptr::NonNull;

use super::PluginError;

/// Create a [`PluginError`] from [`libc::dlerror()`] for `func`.
fn dlerror(func: &str) -> PluginError {
    // SAFETY: Calling dlerror() is safe.
    let dlerror = unsafe { libc::dlerror() };
    if dlerror.is_null() {
        return PluginError::Dl {
            func: func.into(),
            error: String::from("dlerror() returned NULL"),
        };
    }
    // SAFETY: If dlerror() returned non-NULL, it is a NULL-terminated string.
    let err = unsafe { std::ffi::CStr::from_ptr(dlerror) };
    PluginError::Dl {
        func: func.into(),
        error: err
            .to_str()
            .unwrap_or("invalid string returned from dlerror()")
            .to_string(),
    }
}

/// A handle to a dynamically loaded library.
#[derive(Debug)]
pub(super) struct DynLib(NonNull<libc::c_void>);

impl DynLib {
    /// Create a `DynLib`. Panics if filename is not a UTF-8 string.
    pub(super) fn new(filename: &str) -> Result<Self, PluginError> {
        let filename_c = std::ffi::CString::new(filename).unwrap();
        match NonNull::new(unsafe { libc::dlopen(filename_c.as_ptr(), libc::RTLD_NOW) }) {
            Some(handle) => Ok(DynLib(handle)),
            None => Err(dlerror("dlopen")),
        }
    }

    /// Returns the address of the `symbol` where that symbol is loaded into
    /// memory.
    ///
    /// The returned address is only valid when `self` is alive.
    ///
    /// Panics if symbol is not a valid UTF-8 string.
    pub(super) fn sym(&self, symbol: &str) -> Result<NonNull<libc::c_void>, PluginError> {
        let symbol_c = std::ffi::CString::new(symbol).unwrap();
        match NonNull::new(unsafe { libc::dlsym(self.0.as_ptr(), symbol_c.as_ptr()) }) {
            Some(sym) => Ok(sym),
            None => Err(dlerror("dlsym")),
        }
    }
}

impl Drop for DynLib {
    fn drop(&mut self) {
        // SAFETY: self.0 was returned by dlopen().
        unsafe {
            libc::dlclose(self.0.as_ptr());
        }
    }
}

#[cfg(test)]
mod tests {
    use assert_matches::assert_matches;

    use super::dlerror;
    use super::DynLib;
    use crate::processors::PluginError;

    #[test]
    fn dl_libc_isdigit() {
        let handle = DynLib::new("libc.so.6").unwrap();
        let isdigit: unsafe extern "C" fn(libc::c_int) -> libc::c_int =
            unsafe { std::mem::transmute(handle.sym("isdigit").unwrap()) };
        assert_ne!(unsafe { isdigit('1' as libc::c_int) }, 0);
        assert_eq!(unsafe { isdigit('a' as libc::c_int) }, 0);
    }

    #[test]
    fn dl_does_not_exist() {
        let handle = DynLib::new("/does/not/exist.so");
        assert_matches!(handle, Err(PluginError::Dl { .. }));
    }

    #[test]
    fn dl_libc_does_not_exist() {
        let handle = DynLib::new("libc.so.6").unwrap();
        let does_not_exist = handle.sym("D0ESnOtExisT");
        assert_matches!(does_not_exist, Err(PluginError::Dl { .. }));
    }

    #[test]
    fn dlerror_null() {
        dlerror("first call");
        // dlerror() returns NULL if no errors have occurred since it was last called.
        let err = dlerror("second call");
        match err {
            PluginError::Dl { func, error } => {
                assert_eq!(func, "second call");
                assert_eq!(error, "dlerror() returned NULL");
            }
            _ => panic!("incorrect error returned: {:?}", err),
        }
    }
}
