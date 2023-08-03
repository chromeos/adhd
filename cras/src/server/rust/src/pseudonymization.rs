// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use std::convert::TryInto;
use std::fmt::Display;

use nix::errno::Errno;

// Follow Chrome to use a uint32_t salt.
// https://source.chromium.org/chromium/chromium/src/+/main:content/common/pseudonymization_salt.h;l=29;drc=ff1e8456f8d816467989f28df302a3c85975352d
#[repr(C)]
pub struct Salt(Option<u32>);

impl Salt {
    fn new() -> Result<Self, getrandom::Error> {
        let mut buf = [0u8; 4];
        getrandom::getrandom(&mut buf)?;
        Ok(Self(Some(u32::from_ne_bytes(buf))))
    }

    fn new_bypass() -> Self {
        Self(None)
    }

    /// Create a new Salt from the environment variable CRAS_PSEUDONYMIZATION_SALT.
    /// If the environment variable is not set, then a new salt is generated randomly.
    /// If the environment variable is set, then it must be a valid number representing
    /// the salt.
    /// If the environment variable is set to the string "none", then the salt
    /// is created in bypass mode.
    fn new_from_environment() -> Result<Self, Errno> {
        match std::env::var("CRAS_PSEUDONYMIZATION_SALT") {
            Ok(string) => match string.as_str() {
                "none" => Ok(Self::new_bypass()),
                string => match string.parse() {
                    Ok(number) => Ok(Salt(Some(number))),
                    Err(_) => Err(Errno::EINVAL),
                },
            },
            Err(_) => Self::new().map_err(|_| Errno::ENOSYS),
        }
    }

    pub fn instance() -> &'static Salt {
        use once_cell::sync::OnceCell;
        static INSTANCE: OnceCell<Salt> = OnceCell::new();
        INSTANCE.get_or_init(|| Salt::new_from_environment().expect("Cannot initialize new salt"))
    }

    fn pseudonymize_stable_id(&self, stable_id: u32) -> u32 {
        match self.0 {
            None => stable_id,
            Some(salt) => {
                let mut sha1 = openssl::sha::Sha1::new();
                sha1.update(&stable_id.to_ne_bytes());
                sha1.update(&salt.to_ne_bytes());
                let buf: [u8; 4] = sha1.finish()[..4].try_into().unwrap();
                u32::from_ne_bytes(buf)
            }
        }
    }
}

impl Display for Salt {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        match self.0 {
            None => "none".fmt(f),
            Some(salt) => salt.fmt(f),
        }
    }
}

#[cfg(test)]
mod tests {
    use crate::pseudonymization::Salt;

    #[test]
    fn stable_id() {
        let salt1 = Salt::new().unwrap();
        let salt2 = Salt::new().unwrap();
        assert_ne!(salt1.pseudonymize_stable_id(0), 0);
        assert_ne!(
            salt1.pseudonymize_stable_id(0),
            salt2.pseudonymize_stable_id(0)
        );
        assert_eq!(
            salt1.pseudonymize_stable_id(0),
            salt1.pseudonymize_stable_id(0)
        );
    }
}

pub mod bindings {
    use super::Salt;

    #[no_mangle]
    /// Pseudonymize the stable_id using the global salt.
    /// Returns the salted stable_id.
    pub extern "C" fn pseudonymize_stable_id(stable_id: u32) -> u32 {
        Salt::instance().pseudonymize_stable_id(stable_id)
    }
}
