// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use std::borrow::Cow;
use std::ffi::CString;
use std::fmt::Display;

use anyhow::bail;
use bitflags::bitflags;
use itertools::Itertools;

bitflags! {
    #[allow(non_camel_case_types)]
    #[repr(transparent)]
    pub struct CRAS_STREAM_ACTIVE_AP_EFFECT: u64 {
        const ECHO_CANCELLATION = 1 << 0;
        const NOISE_SUPPRESSION = 1 << 1;
        const VOICE_ACTIVITY_DETECTION = 1 << 2;
        const NEGATE = 1 << 3;
        const NOISE_CANCELLATION = 1 << 4;
        const STYLE_TRANSFER = 1 << 5;
        const BEAMFORMING = 1 << 6;
        const PROCESSOR_OVERRIDDEN = 1 << 7;
    }
}

impl CRAS_STREAM_ACTIVE_AP_EFFECT {
    pub fn joined_name(&self) -> Cow<str> {
        if self.is_empty() {
            Cow::Borrowed("none")
        } else {
            Cow::Owned(self.iter_names().map(|(s, _)| s.to_lowercase()).join(" "))
        }
    }
}

/// Returns the names of active effects as a string.
/// The resulting string should be freed with cras_rust_free_string.
#[no_mangle]
pub extern "C" fn cras_stream_active_ap_effects_string(
    effect: CRAS_STREAM_ACTIVE_AP_EFFECT,
) -> *mut libc::c_char {
    CString::new(effect.joined_name().as_ref())
        .unwrap()
        .into_raw()
}

/// All supported DLCs in CRAS.
#[repr(C)]
#[derive(Clone, Copy, PartialEq, Hash, Eq, Debug)]
pub enum CrasDlcId {
    CrasDlcSrBt,
    CrasDlcNcAp,
    CrasDlcIntelligoBeamforming,
}

// The list of DLCs that are installed automatically.
pub const MANAGED_DLCS: &[CrasDlcId] = &[
    CrasDlcId::CrasDlcSrBt,
    CrasDlcId::CrasDlcNcAp,
    CrasDlcId::CrasDlcIntelligoBeamforming,
];

pub const NUM_CRAS_DLCS: usize = 3;
// Assert that NUM_CRAS_DLCS is updated.
// We cannot assign MANAGED_DLCS.len() to NUM_CRAS_DLCS because cbindgen does
// not seem to understand it.
static_assertions::const_assert_eq!(NUM_CRAS_DLCS, MANAGED_DLCS.len());

pub const CRAS_DLC_ID_STRING_MAX_LENGTH: i32 = 50;
impl CrasDlcId {
    pub fn as_str(&self) -> &'static str {
        match self {
            // The length of these strings should be bounded by
            // CRAS_DLC_ID_STRING_MAX_LENGTH
            CrasDlcId::CrasDlcSrBt => "sr-bt-dlc",
            CrasDlcId::CrasDlcNcAp => "nc-ap-dlc",
            CrasDlcId::CrasDlcIntelligoBeamforming => "intelligo-beamforming-dlc",
        }
    }
}

impl TryFrom<&str> for CrasDlcId {
    type Error = anyhow::Error;

    fn try_from(value: &str) -> anyhow::Result<Self> {
        for dlc in MANAGED_DLCS {
            if dlc.as_str() == value {
                return Ok(dlc.clone());
            }
        }
        bail!("unknown DLC {value}");
    }
}

impl Display for CrasDlcId {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        f.write_str(self.as_str())
    }
}
