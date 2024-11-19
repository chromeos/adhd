// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Bindings for cras_ini.
//!
//! The functions are drop in replacements for the C-based iniparser.

use std::ffi::CStr;
use std::ffi::OsStr;
use std::os::unix::ffi::OsStrExt;
use std::ptr::null_mut;
use std::ptr::NonNull;

use crate::parse_file;
use crate::CrasIniMap;

pub struct CrasIniDict(CrasIniMap);

trait ToMapRef {
    unsafe fn to_map_ref<'a>(self) -> &'a CrasIniMap;
}

impl ToMapRef for *const CrasIniDict {
    unsafe fn to_map_ref<'a>(self) -> &'a CrasIniMap {
        &self.as_ref().unwrap().0
    }
}

/// Load the ini at the given path.
/// Returns NULL and logs on error.
///
/// # Safety
///
/// `ini_name` must be a NULL-terminated string.
#[no_mangle]
pub unsafe extern "C" fn cras_ini_load(ini_path: *const libc::c_char) -> *mut CrasIniDict {
    let ini_name_cstr = CStr::from_ptr(ini_path);
    let map = match parse_file(OsStr::from_bytes(ini_name_cstr.to_bytes()).as_ref()) {
        Ok(dict) => dict,
        Err(err) => {
            log::warn!("failed to load ini file: {err:#}");
            return null_mut();
        }
    };
    Box::into_raw(Box::new(CrasIniDict(map)))
}

/// Free the dict.
///
/// # Safety
///
/// `dict` must be something returned from cras_ini_load().
/// Once a dict is freed it may not be used.
#[no_mangle]
pub unsafe extern "C" fn cras_ini_free(dict: *mut CrasIniDict) {
    match NonNull::new(dict) {
        Some(ptr) => drop(Box::from_raw(ptr.as_ptr())),
        None => (), // Do nothing for null pointer.
    }
}

/// Return the number of sections in this dict.
///
/// # Safety
///
/// `dict` must point to a dict that was returned from cras_ini_load().
#[no_mangle]
pub unsafe extern "C" fn cras_ini_getnsec(dict: *const CrasIniDict) -> libc::c_int {
    dict.to_map_ref().len() as libc::c_int
}

/// Return the name of the i-th section as a NULL-terminated string.
///
/// # Safety
///
/// `dict` must point to a dict that was returned from cras_ini_load().
/// The returned string is alive until dict is freed. Do not free it yourself.
#[no_mangle]
pub unsafe extern "C" fn cras_ini_getsecname(
    dict: *const CrasIniDict,
    i: libc::c_int,
) -> *const libc::c_char {
    dict.to_map_ref()
        .get_index(usize::try_from(i).unwrap())
        .unwrap()
        .0
        .as_cstr()
        .as_ptr()
}

/// Return the number of keys in the section.
///
/// # Safety
///
/// `dict` must point to a dict that was returned from cras_ini_load().
#[no_mangle]
pub unsafe extern "C" fn cras_ini_getsecnkeys(
    dict: *const CrasIniDict,
    section: *const libc::c_char,
) -> libc::c_int {
    dict.to_map_ref()
        .get(CStr::from_ptr(section).to_str().unwrap())
        .unwrap()
        .len() as libc::c_int
}

/// Return the name of the i-th key in the section.
///
/// # Safety
///
/// `dict` must point to a dict that was returned from cras_ini_load().
/// The returned string is alive until dict is freed. Do not free it yourself.
#[no_mangle]
pub unsafe extern "C" fn cras_ini_getseckey(
    dict: *const CrasIniDict,
    section: *const libc::c_char,
    i: libc::c_int,
) -> *const libc::c_char {
    dict.to_map_ref()
        .get(CStr::from_ptr(section).to_str().unwrap())
        .unwrap()
        .get_index(usize::try_from(i).unwrap())
        .unwrap()
        .0
        .as_cstr()
        .as_ptr()
}

/// Get the value stored in dict. `section_and_key` is a string formatted as
/// `section_name:key_name`.
/// Returns `notfound` if not found.
///
/// # Safety
///
/// `dict` must point to a dict that was returned from cras_ini_load().
/// The returned string is alive until dict is freed. Do not free it yourself.
#[no_mangle]
pub unsafe extern "C" fn cras_ini_getstring(
    dict: *const CrasIniDict,
    section_and_key: *const libc::c_char,
    notfound: *const libc::c_char,
) -> *const libc::c_char {
    let (section_name, key) = CStr::from_ptr(section_and_key)
        .to_str()
        .expect("not utf-8")
        .split_once(":")
        .expect("`:` not found");

    match dict
        .to_map_ref()
        .get(section_name)
        .and_then(|section| section.get(key))
    {
        Some(value) => value.as_cstr().as_ptr(),
        None => notfound,
    }
}

/// Get the value stored in dict. `section_and_key` is a string formatted as
/// `section_name:key_name`. The value is parsed with atoi.
/// Returns `notfound` if not found.
///
/// # Safety
///
/// `dict` must point to a dict that was returned from cras_ini_load().
#[no_mangle]
pub unsafe extern "C" fn cras_ini_getint(
    dict: *const CrasIniDict,
    section_and_key: *const libc::c_char,
    notfound: libc::c_int,
) -> libc::c_int {
    let (section_name, key) = CStr::from_ptr(section_and_key)
        .to_str()
        .expect("not utf-8")
        .split_once(":")
        .expect("`:` not found");

    match dict
        .to_map_ref()
        .get(section_name)
        .and_then(|section| section.get(key))
    {
        Some(value) => libc::atoi(value.as_cstr().as_ptr()),
        None => notfound,
    }
}

#[cfg(test)]
mod tests {
    use std::ptr::null;
    use std::ptr::null_mut;

    use super::*;
    use crate::parse_string;

    fn new_dict(s: &str) -> CrasIniDict {
        CrasIniDict(parse_string(s).unwrap())
    }

    #[test]
    fn free() {
        let dict = Box::into_raw(Box::new(new_dict("")));
        unsafe {
            cras_ini_free(dict);
        }
    }

    #[test]
    fn free_null() {
        unsafe {
            cras_ini_free(null_mut());
        }
    }

    #[test]
    fn getnsec() {
        let dict = new_dict("[a]\n[b]");
        assert_eq!(unsafe { cras_ini_getnsec(&dict) }, 2);
    }

    #[test]
    fn getsecname() {
        let dict = new_dict("[a]");
        assert_eq!(
            unsafe { CStr::from_ptr(cras_ini_getsecname(&dict, 0)) },
            c"a"
        );
    }

    #[test]
    fn getsecnkeys() {
        let dict = new_dict("[aa]\nx=1\ny=2");
        assert_eq!(unsafe { cras_ini_getsecnkeys(&dict, c"aa".as_ptr()) }, 2);
    }

    #[test]
    fn getseckey() {
        let dict = new_dict("[aa]\nx=1\ny=2");
        assert_eq!(
            unsafe { CStr::from_ptr(cras_ini_getseckey(&dict, c"aa".as_ptr(), 0)) },
            c"x",
        );
        assert_eq!(
            unsafe { CStr::from_ptr(cras_ini_getseckey(&dict, c"aa".as_ptr(), 1)) },
            c"y",
        );
    }

    #[test]
    fn getstring() {
        let dict = new_dict("[a]\nx=y");
        assert_eq!(
            unsafe { CStr::from_ptr(cras_ini_getstring(&dict, c"a:x".as_ptr(), null())) },
            c"y"
        );
    }

    #[test]
    fn getstring_notfound() {
        let dict = new_dict("[a]");
        assert_eq!(
            unsafe { CStr::from_ptr(cras_ini_getstring(&dict, c"a:x".as_ptr(), c"zzz".as_ptr())) },
            c"zzz"
        )
    }

    #[test]
    fn getint() {
        let dict = new_dict("[a]\nx=100");
        assert_eq!(unsafe { cras_ini_getint(&dict, c"a:x".as_ptr(), 0) }, 100);
    }

    #[test]
    fn getint_notint() {
        let dict = new_dict("[a]\nx=y");
        assert_eq!(unsafe { cras_ini_getint(&dict, c"a:x".as_ptr(), 1234) }, 0);
    }

    #[test]
    fn getint_notfound() {
        let dict = new_dict("[a]");
        assert_eq!(
            unsafe { cras_ini_getint(&dict, c"a:x".as_ptr(), 5678) },
            5678
        );
    }
}
