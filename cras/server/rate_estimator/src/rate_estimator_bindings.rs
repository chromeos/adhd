// Copyright 2019 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use std::time::Duration;

use libc;

use crate::rate_estimator::RateEstimator;
use crate::rate_estimator::RateEstimatorImpl;
use crate::rate_estimator::RateEstimatorStub;

// An extra indirection with `Box` is needed because dyn trait objects
// don't have a safe ABI and cannot be used across the FFI boundary.
pub type RateEstimatorHandle = Box<dyn RateEstimator>;

/// # Safety
///
/// To use this function safely, `window_size` must be a valid pointer to a
/// timespec.
#[no_mangle]
pub unsafe extern "C" fn rate_estimator_create(
    rate: libc::c_uint,
    window_size: *const libc::timespec,
    smooth_factor: libc::c_double,
) -> *mut RateEstimatorHandle {
    if window_size.is_null() {
        return std::ptr::null_mut::<RateEstimatorHandle>();
    }

    let ts = &*window_size;
    let window = Duration::new(ts.tv_sec as u64, ts.tv_nsec as u32);

    match RateEstimatorImpl::try_new(rate, window, smooth_factor) {
        Ok(re) => Box::into_raw(Box::new(Box::new(re))),
        Err(_) => std::ptr::null_mut::<RateEstimatorHandle>(),
    }
}

/// Create a stub rate estimator for testing.
#[no_mangle]
pub extern "C" fn rate_estimator_create_stub() -> *mut RateEstimatorHandle {
    Box::into_raw(Box::new(Box::new(RateEstimatorStub::default())))
}

/// # Safety
///
/// To use this function safely, `re` must be a pointer returned from
/// rate_estimator_create*, or null.
#[no_mangle]
pub unsafe extern "C" fn rate_estimator_destroy(re: *mut RateEstimatorHandle) {
    if re.is_null() {
        return;
    }

    drop(Box::from_raw(re));
}

/// # Safety
///
/// To use this function safely, `re` must be a pointer returned from
/// rate_estimator_create*, or null.
#[no_mangle]
pub unsafe extern "C" fn rate_estimator_add_frames(
    re: *mut RateEstimatorHandle,
    frames: libc::c_int,
) -> bool {
    if re.is_null() {
        return false;
    }

    (*re).add_frames(frames)
}

/// # Safety
///
/// To use this function safely, `re` must be a pointer returned from
/// rate_estimator_create*, or null, and `now` must be a valid pointer to a
/// timespec.
#[no_mangle]
pub unsafe extern "C" fn rate_estimator_check(
    re: *mut RateEstimatorHandle,
    level: libc::c_int,
    now: *const libc::timespec,
) -> i32 {
    if re.is_null() || now.is_null() {
        return 0;
    }

    let ts = &*now;
    if ts.tv_sec < 0 || ts.tv_nsec < 0 {
        return 0;
    }
    let secs = ts.tv_sec as u64 + (ts.tv_nsec / 1_000_000_000) as u64;
    let nsecs = (ts.tv_nsec % 1_000_000_000) as u32;
    let now = Duration::new(secs, nsecs);

    (*re).update_estimated_rate(level, now) as i32
}

/// # Safety
///
/// To use this function safely, `re` must be a pointer returned from
/// rate_estimator_create, or null.
#[no_mangle]
pub unsafe extern "C" fn rate_estimator_get_rate(re: *const RateEstimatorHandle) -> libc::c_double {
    if re.is_null() {
        return 0.0;
    }

    (*re).get_estimated_rate()
}

/// # Safety
///
/// To use this function safely, `re` must be a pointer returned from
/// rate_estimator_create, or null.
#[no_mangle]
pub unsafe extern "C" fn rate_estimator_reset_rate(
    re: *mut RateEstimatorHandle,
    rate: libc::c_uint,
) {
    if re.is_null() {
        return;
    }

    (*re).reset_rate(rate)
}

/// # Safety
///
/// To use this function safely, `re` must be a pointer returned from
/// rate_estimator_create_stub.
#[no_mangle]
pub unsafe extern "C" fn rate_estimator_get_last_add_frames_value_for_test(
    re: *const RateEstimatorHandle,
) -> i32 {
    re.as_ref().unwrap().get_last_add_frames_value_for_test()
}

/// # Safety
///
/// To use this function safely, `re` must be a pointer returned from
/// rate_estimator_create_stub.
#[no_mangle]
pub unsafe extern "C" fn rate_estimator_get_add_frames_called_count_for_test(
    re: *const RateEstimatorHandle,
) -> u64 {
    re.as_ref().unwrap().get_add_frames_called_count_for_test()
}
