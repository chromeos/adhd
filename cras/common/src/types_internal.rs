// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use std::borrow::Cow;
use std::ffi::CStr;
use std::ffi::CString;
use std::fmt::Display;

use anyhow::bail;
use bitflags::bitflags;
use itertools::Itertools;
use serde::Serialize;

#[repr(C)]
#[derive(Clone, Copy, Debug)]
pub enum CrasProcessorEffect {
    NoEffects,
    Negate,
    NoiseCancellation,
    StyleTransfer,
    Beamforming,
    Overridden,
}

impl CrasProcessorEffect {
    fn as_c_str(&self) -> &CStr {
        match self {
            CrasProcessorEffect::NoEffects => c"NoEffects",
            CrasProcessorEffect::Negate => c"Negate",
            CrasProcessorEffect::NoiseCancellation => c"NoiseCancellation",
            CrasProcessorEffect::StyleTransfer => c"StyleTransfer",
            CrasProcessorEffect::Beamforming => c"Beamforming",
            CrasProcessorEffect::Overridden => c"Overridden",
        }
    }
}

/// Returns the name of the CrasProcessorEffect as a string.
/// The ownership of the string is static in Rust, so no need to free in C.
#[no_mangle]
pub extern "C" fn cras_processor_effect_to_str(effect: CrasProcessorEffect) -> *const libc::c_char {
    effect.as_c_str().as_ptr()
}

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

// The bitmask enum of audio effects. Bit is toggled on for supporting.
// This should be always aligned to platform2/system_api/dbus/audio/dbus-constants.h.
bitflags! {
    #[allow(non_camel_case_types)]
    #[repr(transparent)]
    #[derive(Clone, Copy, PartialEq, Hash, Eq, Debug, Serialize)]
    pub struct EFFECT_TYPE: u32 {
      const NONE = 0;
      const NOISE_CANCELLATION = 1 << 0;
      const HFP_MIC_SR = 1 << 1;
      const STYLE_TRANSFER = 1 << 2;
      const BEAMFORMING = 1 << 3;
    }
}

bitflags! {
    #[allow(non_camel_case_types)]
    #[repr(transparent)]
    #[derive(Clone, Copy, PartialEq, Hash, Eq, Debug, Serialize, Default)]
    pub struct CRAS_NC_PROVIDER : u32 {
        const NONE = 0;      // NC is disabled for this ionode.
        const DSP = 1 << 0;  // NC is supported by DSP.
        const AP = 1 << 1;   // NC is supported by AP.
        const AST = 1 << 2;  // NC is supported by Style Transfer.
        const BF = 1 << 3;   // NC is supported by Beamforming.
    }
}

impl CRAS_NC_PROVIDER {
    fn as_c_str(&self) -> &CStr {
        match self {
            &CRAS_NC_PROVIDER::NONE => c"CRAS_NC_PROVIDER_NONE",
            &CRAS_NC_PROVIDER::DSP => c"CRAS_NC_PROVIDER_DSP",
            &CRAS_NC_PROVIDER::AP => c"CRAS_NC_PROVIDER_AP",
            &CRAS_NC_PROVIDER::AST => c"CRAS_NC_PROVIDER_AST",
            &CRAS_NC_PROVIDER::BF => c"CRAS_NC_PROVIDER_BF",
            _ => c"Invalid NC provider",
        }
    }

    pub fn joined_name(&self) -> Cow<str> {
        if self.is_empty() {
            Cow::Borrowed("none")
        } else {
            Cow::Owned(self.iter_names().map(|(s, _)| s.to_lowercase()).join(" "))
        }
    }
}

pub const CRAS_NC_PROVIDER_PREFERENCE_ORDER: &[CRAS_NC_PROVIDER] = &[
    CRAS_NC_PROVIDER::AST,
    CRAS_NC_PROVIDER::BF,
    CRAS_NC_PROVIDER::DSP,
    CRAS_NC_PROVIDER::AP,
    CRAS_NC_PROVIDER::NONE,
];

/// Returns the name of the NC provider as a string.
/// The ownership of the string is static in Rust, so no need to free in C.
#[no_mangle]
pub extern "C" fn cras_nc_provider_to_str(nc_provider: CRAS_NC_PROVIDER) -> *const libc::c_char {
    nc_provider.as_c_str().as_ptr()
}

/// Returns the names of the bitset of NC providers as a string.
/// The resulting string should be freed with cras_rust_free_string.
#[no_mangle]
pub extern "C" fn cras_nc_providers_bitset_to_str(
    nc_providers: CRAS_NC_PROVIDER,
) -> *mut libc::c_char {
    CString::new(nc_providers.joined_name().as_ref())
        .unwrap()
        .into_raw()
}

#[repr(C)]
#[derive(Clone, Copy, Debug, Serialize)]
pub struct CrasEffectUIAppearance {
    // Decides which title to show on the toggle, 0 for hidden.
    pub toggle_type: EFFECT_TYPE,
    // Bitset of EFFECT_TYPE, decides which options to show, 0 for hidden.
    pub effect_mode_options: EFFECT_TYPE,
    // Decides whether to show the effect fallback message or not.
    pub show_effect_fallback_message: bool,
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

#[cfg(test)]
mod tests {
    use super::CRAS_NC_PROVIDER;
    use super::CRAS_NC_PROVIDER_PREFERENCE_ORDER;

    #[test]
    fn test_cras_nc_provider_as_c_str() {
        // CRAS_NC_PROVIDER.as_c_str() should return a valid &CStr for
        // every defined bit.
        let invalid = CRAS_NC_PROVIDER::AP | CRAS_NC_PROVIDER::DSP;
        for nc_provider in CRAS_NC_PROVIDER::all().iter() {
            assert_ne!(nc_provider.as_c_str(), invalid.as_c_str());
        }
    }

    #[test]
    fn test_cras_nc_provider_preference_order() {
        assert_eq!(
            CRAS_NC_PROVIDER_PREFERENCE_ORDER.len() as u32,
            CRAS_NC_PROVIDER::all().bits().count_ones() + 1
        );
    }
}
