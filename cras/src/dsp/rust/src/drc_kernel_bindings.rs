// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::drc_kernel::DrcKernel;
use crate::drc_kernel::DrcKernelParam;
use crate::drc_kernel::DRC_NUM_CHANNELS;

/// Initializes a drc kernel
#[no_mangle]
pub unsafe extern "C" fn dk_new(sample_rate: f32) -> *mut DrcKernel {
    Box::into_raw(Box::new(DrcKernel::new(sample_rate)))
}

/// Frees a drc kernel
#[no_mangle]
pub unsafe extern "C" fn dk_free(dk: *mut DrcKernel) {
    if let Some(dk) = dk.as_mut() {
        drop(Box::from_raw(dk));
    }
}

/// Sets the parameters of a drc kernel. See drc.h for details
#[allow(non_snake_case)]
#[no_mangle]
pub unsafe extern "C" fn dk_set_parameters(
    dk: *mut DrcKernel,
    db_threshold: f32,
    db_knee: f32,
    ratio: f32,
    attack_time: f32,
    release_time: f32,
    pre_delay_time: f32,
    db_post_gain: f32,
    releaseZone1: f32,
    releaseZone2: f32,
    releaseZone3: f32,
    releaseZone4: f32,
) {
    if let Some(dk) = dk.as_mut() {
        dk.set_parameters(
            db_threshold,
            db_knee,
            ratio,
            attack_time,
            release_time,
            pre_delay_time,
            db_post_gain,
            releaseZone1,
            releaseZone2,
            releaseZone3,
            releaseZone4,
        );
    }
}

/// Enables or disables a drc kernel
#[no_mangle]
pub unsafe extern "C" fn dk_set_enabled(dk: *mut DrcKernel, enabled: i32) {
    if let Some(dk) = dk.as_mut() {
        dk.set_enabled(enabled != 0);
    }
}

/// Performs stereo-linked compression.
/// Args:
///    dk - The DRC kernel.
///    data - The pointers to the audio sample buffer. One pointer per channel.
///    count - The number of audio samples per channel.
///
#[no_mangle]
pub unsafe extern "C" fn dk_process(dk: *mut DrcKernel, data_channels: *mut *mut f32, count: u32) {
    if count == 0 {
        return;
    }
    if let Some(dk) = dk.as_mut() {
        let data1: &mut [*mut f32] =
            std::slice::from_raw_parts_mut(data_channels, DRC_NUM_CHANNELS);
        let mut data2: [&mut [f32]; DRC_NUM_CHANNELS] = Default::default();
        for (datum1, datum2) in std::iter::zip(data1, &mut data2) {
            *datum2 = std::slice::from_raw_parts_mut(*datum1, count as usize);
        }
        dk.process(&mut data2, count as usize);
    }
}

/// Retrieves and returns the parameters from a DRC kernel, `dk` must be a
/// pointer returned from dk_new.
/// Args:
///    dk - The DRC kernel.
///
#[no_mangle]
pub unsafe extern "C" fn dk_get_parameter(dk: *mut DrcKernel) -> DrcKernelParam {
    dk.as_mut().expect("NULL drc_kernel pointer").param
}
