// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::dcblock::DCBlock;

#[no_mangle]
pub extern "C" fn dcblock_new() -> *mut DCBlock {
    Box::into_raw(Box::new(DCBlock::new()))
}

#[no_mangle]
// To use this function safely, `dcblock` must be a pointer returned from
// dcblock_new, or null.
pub unsafe extern "C" fn dcblock_free(dcblock: *mut DCBlock) {
    if dcblock.is_null() {
        return;
    }
    drop(Box::from_raw(dcblock));
}

#[no_mangle]
// To use this function safely, `dcblock` must be a pointer returned from
// dcblock_new, or null.
pub unsafe extern "C" fn dcblock_set_config(
    dcblock: *mut DCBlock,
    r: f32,
    sample_rate: libc::c_ulong,
) {
    if dcblock.is_null() {
        return;
    }
    (*dcblock).set_config(r, sample_rate as u64);
}

#[no_mangle]
// This is the prototype of the processing loop.
// To use this function safely, `dcblock` must be a pointer returned from
// dcblock_new, or null, and data must be a valid pointer to a floar array.
pub unsafe extern "C" fn dcblock_process(dcblock: *mut DCBlock, data: *mut f32, count: i32) {
    if dcblock.is_null() {
        return;
    }
    (*dcblock).process(unsafe { std::slice::from_raw_parts_mut(data, count as usize) });
}
