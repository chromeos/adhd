// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use std::convert::TryInto;

use nix::errno::Errno;

// Follow Chrome to use a uint32_t salt.
// https://source.chromium.org/chromium/chromium/src/+/main:content/common/pseudonymization_salt.h;l=29;drc=ff1e8456f8d816467989f28df302a3c85975352d
#[repr(C)]
pub struct Salt(u32);

impl Salt {
    pub fn new() -> Result<Self, getrandom::Error> {
        let mut buf = [0u8; 4];
        getrandom::getrandom(&mut buf)?;
        Ok(Self(u32::from_ne_bytes(buf)))
    }

    /// Create a new Salt from the environment variable CRAS_PSEUDONYMIZATION_SALT.
    /// If the environment variable is not set, then a new salt is generated randomly.
    /// If the environment variable is set, then it must be a valid number representing
    /// the salt.
    pub fn new_from_environment() -> Result<Self, Errno> {
        match std::env::var("CRAS_PSEUDONYMIZATION_SALT") {
            Ok(string) => match string.parse() {
                Ok(number) => Ok(Salt(number)),
                Err(_) => Err(Errno::EINVAL),
            },
            Err(_) => Self::new().map_err(|_| Errno::ENOSYS),
        }
    }

    fn pseudonymize_stable_id(&self, stable_id: u32) -> u32 {
        let mut sha1 = openssl::sha::Sha1::new();
        sha1.update(&stable_id.to_ne_bytes());
        sha1.update(&self.0.to_ne_bytes());
        let buf: [u8; 4] = sha1.finish()[..4].try_into().unwrap();
        u32::from_ne_bytes(buf)
    }
}

impl From<Salt> for u32 {
    fn from(salt: Salt) -> Self {
        salt.0
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
    /// Pseudonymize the stable_id using the given salt.
    /// Returns the salted stable_id.
    pub extern "C" fn pseudonymize_stable_id(salt: u32, stable_id: u32) -> u32 {
        Salt(salt).pseudonymize_stable_id(stable_id)
    }

    #[no_mangle]
    /// Gets the salt from the environment variable CRAS_PSEUDONYMIZATION_SALT.
    /// See `Salt::new_from_environment`.
    /// Returns negative errno on failure.
    ///
    /// # Safety
    /// salt must point to a non-NULL u32.
    pub unsafe extern "C" fn pseudonymize_salt_get_from_env(
        mut salt: std::ptr::NonNull<u32>,
    ) -> libc::c_int {
        match Salt::new_from_environment() {
            Ok(result) => {
                *salt.as_mut() = result.into();
                0
            }
            Err(err) => -(err as i32),
        }
    }
}
