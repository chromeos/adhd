// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

/// Support status for CRAS features.
#[repr(C)]
pub struct CrasFeatureTier {
    pub initialized: bool,
    pub is_x86_64_v2: bool,
    pub sr_bt_supported: bool,
    pub ap_nc_supported: bool,
}

impl CrasFeatureTier {
    /// Construct a CrasFeatureTier. `board_name` should be the name of the
    /// reference board. `cpu_name` should be the model name of the CPU.
    pub fn new(board_name: &str, cpu_name: &str) -> Self {
        let x86_64_v2 = is_x86_64_v2();
        Self {
            initialized: true,
            sr_bt_supported: match board_name {
                "eve" | "soraka" | "nautilus" | "nami" | "nocturne" | "rammus" | "fizz" => {
                    !has_substr(cpu_name, &["celeron", "pentium"])
                }
                _ => false,
            },
            ap_nc_supported: match std::env::consts::ARCH {
                "x86_64" => match board_name {
                    "reven" => x86_64_v2,
                    _ => true,
                },
                _ => false,
            },
            is_x86_64_v2: x86_64_v2,
        }
    }
}

#[cfg(any(target_arch = "x86_64"))]
fn is_x86_64_v2() -> bool {
    is_x86_feature_detected!("cmpxchg16b")
        && is_x86_feature_detected!("popcnt")
        && is_x86_feature_detected!("sse3")
        && is_x86_feature_detected!("sse4.1")
        && is_x86_feature_detected!("sse4.2")
        && is_x86_feature_detected!("ssse3")
}

#[cfg(not(any(target_arch = "x86_64")))]
fn is_x86_64_v2() -> bool {
    false
}

// Returns true only if `string` contains any substring in `substrings`.
// Note that it's case-insensitive.
fn has_substr(string: &str, substrings: &[&str]) -> bool {
    let string_lowercase = string.to_lowercase();
    substrings
        .iter()
        .any(|&substr| string_lowercase.contains(&substr.to_lowercase()))
}

#[cfg(test)]
mod tests {
    use crate::feature_tier::has_substr;
    use crate::feature_tier::CrasFeatureTier;

    #[test]
    fn eve_i7() {
        let tier = CrasFeatureTier::new("eve", "Intel(R) Core(TM) i7-7Y75 CPU @ 1.30GHz");
        assert!(tier.sr_bt_supported);
    }

    #[test]
    fn random_board() {
        let tier = CrasFeatureTier::new("random-board", "random");
        assert!(!tier.sr_bt_supported);
    }

    #[test]
    fn fizz_celeron() {
        let tier = CrasFeatureTier::new("fizz", "Celeron-3865U");
        assert!(!tier.sr_bt_supported);
    }

    #[test]
    fn nami_pentium() {
        let tier = CrasFeatureTier::new("nami", "PENTIUM-4417U");
        assert!(!tier.sr_bt_supported);
    }

    #[test]
    fn brya_i7() {
        let tier = CrasFeatureTier::new("nami", "intel Core i7-1260P");
        assert!(tier.sr_bt_supported);
    }

    #[test]
    fn check_has_substr() {
        assert!(has_substr("DisalLoWed", &["abc", "Dis"]));
        assert!(!has_substr("DisalLoWed", &["abc"]));
    }
}

pub mod bindings {
    use std::ffi::CStr;

    pub use super::CrasFeatureTier;

    #[no_mangle]
    /// Initialize the cras feature tier struct.
    /// On error, a negative error code is returned.
    ///
    /// # Safety
    ///
    /// out must be non-NULL.
    pub unsafe extern "C" fn cras_feature_tier_init(
        out: *mut CrasFeatureTier,
        board_name: *const libc::c_char,
        cpu_name: *const libc::c_char,
    ) -> libc::c_int {
        let board_name = if board_name.is_null() {
            ""
        } else {
            match CStr::from_ptr(board_name).to_str() {
                Ok(name) => name,
                Err(_) => return -libc::EINVAL,
            }
        };

        let cpu_name = if cpu_name.is_null() {
            ""
        } else {
            match CStr::from_ptr(cpu_name).to_str() {
                Ok(name) => name,
                Err(_) => return -libc::EINVAL,
            }
        };

        let tier = CrasFeatureTier::new(board_name, cpu_name);

        log::info!(
            "cras_feature_tier initialized with board={:?} cpu={:?} x86_64_v2={:?}",
            board_name,
            cpu_name,
            tier.is_x86_64_v2
        );

        *out = tier;

        0
    }

    #[cfg(test)]
    mod tests {
        use super::*;

        #[test]
        fn null_safety() {
            let mut tier = std::mem::MaybeUninit::<CrasFeatureTier>::zeroed();
            let rc = unsafe {
                cras_feature_tier_init(tier.as_mut_ptr(), std::ptr::null(), std::ptr::null())
            };
            assert_eq!(0, rc);
        }
    }
}
