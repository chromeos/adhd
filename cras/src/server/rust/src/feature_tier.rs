// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

/// Support status for CRAS features.
#[repr(C)]
pub struct CrasFeatureTier {
    pub sr_bt_supported: bool,
}

impl CrasFeatureTier {
    /// Construct a CrasFeatureTier. `board_name` should be the name of the
    /// reference board. `cpu_name` should be the model name of the CPU.
    pub fn new(board_name: &str, cpu_name: &str) -> Self {
        Self {
            sr_bt_supported: match board_name {
                "eve" | "soraka" | "nautilus" | "nami" | "atlas" | "nocturne" | "rammus" => {
                    !cpu_name.to_lowercase().contains("celeron")
                }
                _ => false,
            },
        }
    }
}

#[cfg(test)]
mod tests {
    use crate::feature_tier::CrasFeatureTier;

    #[test]
    fn eve_i7() {
        let tier = CrasFeatureTier::new("eve", "Intel(R) Core(TM) i7-7Y75 CPU @ 1.30GHz");
        assert!(tier.sr_bt_supported);
    }

    #[test]
    fn random_celeron() {
        let tier = CrasFeatureTier::new("random-board", "celeron");
        assert_eq!(tier.sr_bt_supported, false);
    }
}

pub mod bindings {
    use std::ffi::CStr;

    pub use super::CrasFeatureTier;

    #[no_mangle]
    /// Initialize the cras feature tier struct.
    /// On error, a negative error code is returned.
    pub unsafe extern "C" fn cras_feature_tier_init(
        out: *mut CrasFeatureTier,
        board_name: *const libc::c_char,
        cpu_name: *const libc::c_char,
    ) -> libc::c_int {
        let board_name = CStr::from_ptr(board_name);
        let board_name = match board_name.to_str() {
            Ok(name) => name,
            Err(_) => return -libc::EINVAL,
        };
        let cpu_name = CStr::from_ptr(cpu_name);
        let cpu_name = match cpu_name.to_str() {
            Ok(name) => name,
            Err(_) => return -libc::EINVAL,
        };

        *out = CrasFeatureTier::new(board_name, cpu_name);

        0
    }
}