// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use std::convert::TryInto;

// Follow Chrome to use a uint32_t salt.
// https://source.chromium.org/chromium/chromium/src/+/main:content/common/pseudonymization_salt.h;l=29;drc=ff1e8456f8d816467989f28df302a3c85975352d
struct Salt(u32);

impl Salt {
    fn new() -> Result<Self, getrandom::Error> {
        let mut buf = [0u8; 4];
        getrandom::getrandom(&mut buf)?;
        Ok(Self(u32::from_ne_bytes(buf)))
    }

    fn pseudonymize_stable_id(&self, stable_id: u32) -> u32 {
        let mut sha1 = openssl::sha::Sha1::new();
        sha1.update(&stable_id.to_ne_bytes());
        sha1.update(&self.0.to_ne_bytes());
        let buf: [u8; 4] = sha1.finish()[..4].try_into().unwrap();
        u32::from_ne_bytes(buf)
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
