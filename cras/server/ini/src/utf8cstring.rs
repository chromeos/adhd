// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use std::borrow::Borrow;
use std::ffi::CStr;
use std::hash::Hash;

/// A Null-terminated UTF-8 string that can be borrowed as `&str` and `&cstr`.
#[derive(PartialEq, Eq)]
pub struct Utf8CString {
    // Invariant: data must terminated with a single NULL.
    data: String,
}

impl TryFrom<String> for Utf8CString {
    type Error = anyhow::Error;

    fn try_from(mut value: String) -> Result<Self, Self::Error> {
        value.push('\0');
        CStr::from_bytes_with_nul(value.as_bytes())?;
        Ok(Self { data: value })
    }
}

impl Utf8CString {
    /// Return the contained string as a &str.
    pub fn as_str(&self) -> &str {
        self.data.strip_suffix('\0').unwrap()
    }

    /// Return the contained string as a &CStr.
    pub fn as_cstr(&self) -> &CStr {
        CStr::from_bytes_with_nul(self.data.as_bytes()).unwrap()
    }
}

impl Borrow<str> for Utf8CString {
    fn borrow(&self) -> &str {
        self.as_str()
    }
}

impl Hash for Utf8CString {
    fn hash<H: std::hash::Hasher>(&self, state: &mut H) {
        self.as_str().hash(state);
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn has_null() {
        assert!(Utf8CString::try_from(String::from("hello\0world")).is_err());
        assert!(Utf8CString::try_from(String::from("hello world\0")).is_err());
    }

    #[test]
    fn roundtrip_str() {
        let s = "hello world";
        assert_eq!(Utf8CString::try_from(s.to_string()).unwrap().as_str(), s);
    }

    #[test]
    fn roundtrip_cstr() {
        let s = "hello world";
        assert_eq!(
            Utf8CString::try_from(s.to_string()).unwrap().as_cstr(),
            c"hello world"
        );
    }
}
