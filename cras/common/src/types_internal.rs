// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use std::borrow::Cow;
use std::ffi::CStr;
use std::ffi::CString;

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

    fn to_active_ap_effects(&self) -> CRAS_STREAM_ACTIVE_AP_EFFECT {
        match self {
            CrasProcessorEffect::NoEffects => CRAS_STREAM_ACTIVE_AP_EFFECT::empty(),
            CrasProcessorEffect::Negate => CRAS_STREAM_ACTIVE_AP_EFFECT::NEGATE,
            CrasProcessorEffect::NoiseCancellation => {
                CRAS_STREAM_ACTIVE_AP_EFFECT::NOISE_CANCELLATION
            }
            CrasProcessorEffect::StyleTransfer => {
                // Style transfer implies noise cancellation.
                CRAS_STREAM_ACTIVE_AP_EFFECT::NOISE_CANCELLATION
                    | CRAS_STREAM_ACTIVE_AP_EFFECT::STYLE_TRANSFER
            }
            CrasProcessorEffect::Beamforming => {
                // Beamforming is a variant of noise cancellation.
                CRAS_STREAM_ACTIVE_AP_EFFECT::NOISE_CANCELLATION
                    | CRAS_STREAM_ACTIVE_AP_EFFECT::BEAMFORMING
            }
            CrasProcessorEffect::Overridden => CRAS_STREAM_ACTIVE_AP_EFFECT::PROCESSOR_OVERRIDDEN,
        }
    }
}

/// Returns the name of the CrasProcessorEffect as a string.
/// The ownership of the string is static in Rust, so no need to free in C.
#[no_mangle]
pub extern "C" fn cras_processor_effect_to_str(effect: CrasProcessorEffect) -> *const libc::c_char {
    effect.as_c_str().as_ptr()
}

#[no_mangle]
pub extern "C" fn cras_processor_effect_to_active_ap_effects(
    effect: CrasProcessorEffect,
) -> CRAS_STREAM_ACTIVE_AP_EFFECT {
    effect.to_active_ap_effects()
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
    #[derive(Clone, Copy, PartialEq, Hash, Eq, Debug, Serialize, Default)]
    pub struct EFFECT_TYPE: u32 {
      const NONE = 0;
      const NOISE_CANCELLATION = 1 << 0;
      const HFP_MIC_SR = 1 << 1;
      const STYLE_TRANSFER = 1 << 2;
      const BEAMFORMING = 1 << 3;
    }
}

impl EFFECT_TYPE {
    fn as_c_str(&self) -> &CStr {
        match self {
            &EFFECT_TYPE::NONE => c"EFFECT_TYPE_NONE",
            &EFFECT_TYPE::NOISE_CANCELLATION => c"EFFECT_TYPE_NOISE_CANCELLATION",
            &EFFECT_TYPE::STYLE_TRANSFER => c"EFFECT_TYPE_STYLE_TRANSFER",
            &EFFECT_TYPE::BEAMFORMING => c"EFFECT_TYPE_BEAMFORMING",
            _ => c"Invalid NC provider",
        }
    }
}

/// Returns the name of the effect type as a string.
/// The ownership of the string is static in Rust, so no need to free in C.
#[no_mangle]
pub extern "C" fn cras_effect_type_to_str(effect_type: EFFECT_TYPE) -> *const libc::c_char {
    effect_type.as_c_str().as_ptr()
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
    CRAS_NC_PROVIDER::BF,
    CRAS_NC_PROVIDER::AST,
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
#[derive(Clone, Copy, Debug, Serialize, PartialEq, Eq)]
pub struct CrasEffectUIAppearance {
    // Decides which title to show on the toggle, 0 for hidden.
    pub toggle_type: EFFECT_TYPE,
    // Bitset of EFFECT_TYPE, decides which options to show, 0 for hidden.
    pub effect_mode_options: EFFECT_TYPE,
    // Decides whether to show the effect fallback message or not.
    pub show_effect_fallback_message: bool,
}

pub const SR_BT_DLC: &str = "sr-bt-dlc";
pub const NC_AP_DLC: &str = "nc-ap-dlc";

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
