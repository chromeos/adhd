// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::drc::DRCComponent;
use crate::drc::DRC;
use crate::drc::DRC_PARAM;
use crate::drc_kernel::DrcKernel;
use crate::drc_kernel::DRC_NUM_CHANNELS;

/// Allocates a DRC.
#[no_mangle]
pub unsafe extern "C" fn drc_new(sample_rate: f32) -> *mut DRC {
    Box::into_raw(Box::new(DRC::new(sample_rate)))
}

/// Initializes a DRC.
#[no_mangle]
pub unsafe extern "C" fn drc_init(drc: *mut DRC) {
    if let Some(drc) = drc.as_mut() {
        drc.init();
    }
}

/// Frees a DRC.
#[no_mangle]
pub unsafe extern "C" fn drc_free(drc: *mut DRC) {
    if let Some(drc) = drc.as_mut() {
        drop(Box::from_raw(drc));
    }
}

/// Processes input data using a DRC.
/// Args:
///    drc - The DRC we want to use.
///    float **data - Pointers to input/output data. The input must be stereo
///        and one channel is pointed by data[0], another pointed by data[1]. The
///        output data is stored in the same place.
///    frames - The number of frames to process.
///
#[no_mangle]
pub unsafe extern "C" fn drc_process(drc: *mut DRC, data: *mut *mut f32, frames: i32) {
    if frames == 0 {
        return;
    }
    if let Some(drc) = drc.as_mut() {
        let data1: &mut [*mut f32] = std::slice::from_raw_parts_mut(data, DRC_NUM_CHANNELS);
        let mut data2: [&mut [f32]; DRC_NUM_CHANNELS] = Default::default();
        for i in 0..DRC_NUM_CHANNELS {
            data2[i] = std::slice::from_raw_parts_mut(data1[i], frames as usize);
        }
        drc.process(&mut data2, frames as usize);
    }
}

/* Sets a parameter for the DRC.
 * Args:
 *    drc - The DRC we want to use.
 *    index - The index of the kernel we want to set its parameter.
 *    paramID - One of the PARAM_* enum constant.
 *    value - The parameter value
 */
#[allow(non_snake_case)]
#[no_mangle]
pub unsafe extern "C" fn drc_set_param(drc: *mut DRC, index: i32, paramID: u32, value: f32) {
    if let Some(drc) = drc.as_mut() {
        let param_id = DRC_PARAM::try_from(paramID);
        if param_id.is_err() {
            return;
        }
        drc.set_param(index as usize, param_id.unwrap(), value);
    }
}

/// Retrive the components from a DRC structure
/// Args:
///    drc - The DRC kernel.
///
#[no_mangle]
pub unsafe extern "C" fn drc_get_components(drc: *mut DRC) -> DRCComponent {
    if let Some(drc) = drc.as_mut() {
        return DRCComponent {
            emphasis_disabled: drc.emphasis_disabled,
            parameters: drc.parameters,
            emphasis_eq: &drc.emphasis_eq,
            deemphasis_eq: &drc.deemphasis_eq,
            xo2: &drc.xo2,
            kernel: std::array::from_fn(|i| &drc.kernel[i] as *const DrcKernel),
        };
    }
    panic!("NULL drc pointer is used");
}

#[no_mangle]
pub unsafe extern "C" fn drc_set_emphasis_disabled(drc: *mut DRC, value: i32) {
    if let Some(drc) = drc.as_mut() {
        drc.emphasis_disabled = value != 0;
    }
}

#[cfg(test)]
mod tests {
    use crate::drc::DRC;
    use crate::drc::DRC_PARAM;

    #[test]
    fn match_drc_enum_test() {
        for i in 0..DRC_PARAM::PARAM_LAST as usize {
            assert_eq!(i, DRC_PARAM::try_from(i as u32).unwrap() as usize);
        }
        assert!(DRC_PARAM::try_from(DRC_PARAM::PARAM_LAST as u32).is_err());
    }
}
