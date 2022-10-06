// Copyright 2020 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! This module provides different implementations of `Elem` that use the alsa-lib control interface
//! API to read and write alsa control elements.
//!
//! The `Elem::type()` returns the type of value that a control element can interact with,
//! and it is one of integer, integer64, boolean, enumerators, bytes or IEC958 structure.
//! The `Elem::size()` returns the number of values it reads from or writes to the hardware
//! at a time.
//! The `Elem::load(..)` and `Elem::save(..)` are used by `ControlOps` trait to read and write
//! the underlying mixer control.
//!
//! Users should use the provided implementations of `Elem` to define the associated type in
//! their owner encapsulation of `Control`.

use std::default::Default;
use std::error;
use std::fmt::{self, Debug};

use libc::{c_long, c_uchar, c_uint};
use log::debug;
use remain::sorted;

use crate::control_primitive::{self, snd_strerror, Ctl, ElemId, ElemInfo, ElemType, ElemValue};

/// The Result type of cros-alsa::elem.
pub type Result<T> = std::result::Result<T, Error>;

#[sorted]
#[derive(Debug)]
/// Possible errors that can occur in cros-alsa::elem.
pub enum Error {
    /// Failed to call AlsaControlAPI.
    AlsaControlAPI(control_primitive::Error),
    /// Failed to call `snd_ctl_elem_read()`.
    ElemReadFailed(i32),
    /// Failed to call `snd_ctl_elem_write()`.
    ElemWriteFailed(i32),
    /// Invalid Enum value for a Enumerated Control.
    InvalidEnumValue(String, u32, u32),
}

impl error::Error for Error {}

impl From<control_primitive::Error> for Error {
    fn from(err: control_primitive::Error) -> Error {
        Error::AlsaControlAPI(err)
    }
}

impl fmt::Display for Error {
    fn fmt(&self, f: &mut fmt::Formatter) -> fmt::Result {
        use Error::*;
        match self {
            AlsaControlAPI(e) => write!(f, "{}", e),
            ElemReadFailed(e) => write!(f, "snd_ctl_elem_read failed: {}", snd_strerror(*e)?),
            ElemWriteFailed(e) => write!(f, "snd_ctl_elem_write failed: {}", snd_strerror(*e)?),
            InvalidEnumValue(id, val, max) => write!(
                f,
                "expect enum value less than: {}, got: {} for ctrl: {}",
                max, val, id
            ),
        }
    }
}

impl<V: CtlElemValue, const N: usize> Elem for [V; N] {
    type T = [<V as CtlElemValue>::T; N];
    /// Reads [V; N] data from the mixer control.
    ///
    /// # Errors
    ///
    /// * If it fails to call `snd_ctl_elem_read()`.
    fn load(handle: &mut Ctl, id: &ElemId) -> Result<Self::T> {
        let mut elem = ElemValue::new(id)?;
        // Safe because self.handle.as_mut_ptr() is a valid *mut snd_ctl_t and
        // elem.as_mut_ptr() is also a valid *mut snd_ctl_elem_value_t.
        let rc = unsafe { alsa_sys::snd_ctl_elem_read(handle.as_mut_ptr(), elem.as_mut_ptr()) };
        if rc < 0 {
            return Err(Error::ElemReadFailed(rc));
        }
        let mut ret = [Default::default(); N];
        for (i, val) in ret.iter_mut().enumerate().take(N) {
            // Safe because elem.as_ptr() is a valid snd_ctl_elem_value_t* and i is guaranteed to be
            // within a valid range.
            *val = unsafe { V::elem_value_get(&elem, i) };
        }
        Ok(ret)
    }

    /// Updates [V; N] data to the mixer control.
    ///
    /// # Results
    ///
    /// * `changed` - false on success.
    ///             - true on success when value was changed.
    ///
    /// # Errors
    ///
    /// * If it fails to call `snd_ctl_elem_write()`.
    fn save(handle: &mut Ctl, id: &ElemId, val: Self::T) -> Result<bool> {
        let mut elem = ElemValue::new(id)?;
        for i in 0..N {
            // Safe because elem.as_mut_ptr() is a valid snd_ctl_elem_value_t* and i is guaranteed to be
            // within a valid range.
            V::elem_value_validate(handle, id, val[i])?;
            unsafe { V::elem_value_set(&mut elem, i, val[i]) };
        }
        // Safe because self.handle.as_mut_ptr() is a valid *mut snd_ctl_t and
        // elem.as_mut_ptr() is also a valid *mut snd_ctl_elem_value_t.
        let rc = unsafe { alsa_sys::snd_ctl_elem_write(handle.as_mut_ptr(), elem.as_mut_ptr()) };
        if rc < 0 {
            return Err(Error::ElemWriteFailed(rc));
        }
        debug!("set {}: {:#?}", id.name()?, val);
        Ok(rc > 0)
    }

    /// Gets the data type itself can read and write.
    fn elem_type() -> ElemType {
        V::elem_type()
    }

    /// Gets the number of value entries itself can read and write.
    fn size() -> usize {
        N
    }
}

impl CtlElemValue for bool {
    type T = bool;
    /// Gets a bool from the ElemValue.
    unsafe fn elem_value_get(elem: &ElemValue, idx: usize) -> bool {
        alsa_sys::snd_ctl_elem_value_get_boolean(elem.as_ptr(), idx as c_uint) != 0
    }
    /// Sets a bool to the ElemValue.
    unsafe fn elem_value_set(elem: &mut ElemValue, idx: usize, val: bool) {
        alsa_sys::snd_ctl_elem_value_set_boolean(elem.as_mut_ptr(), idx as c_uint, val as c_long);
    }
    /// Returns ElemType::Boolean.
    fn elem_type() -> ElemType {
        ElemType::Boolean
    }
}

impl CtlElemValue for i32 {
    type T = i32;
    /// Gets an i32 from the ElemValue.
    unsafe fn elem_value_get(elem: &ElemValue, idx: usize) -> i32 {
        alsa_sys::snd_ctl_elem_value_get_integer(elem.as_ptr(), idx as c_uint) as i32
    }
    /// Sets an i32 to the ElemValue.
    unsafe fn elem_value_set(elem: &mut ElemValue, idx: usize, val: i32) {
        alsa_sys::snd_ctl_elem_value_set_integer(elem.as_mut_ptr(), idx as c_uint, val as c_long);
    }
    /// Returns ElemType::Integer.
    fn elem_type() -> ElemType {
        ElemType::Integer
    }
}

impl CtlElemValue for u32 {
    type T = u32;
    /// Gets an u32 from the ElemValue.
    unsafe fn elem_value_get(elem: &ElemValue, idx: usize) -> u32 {
        alsa_sys::snd_ctl_elem_value_get_enumerated(elem.as_ptr(), idx as c_uint) as u32
    }
    /// Sets an u32 to the ElemValue.
    unsafe fn elem_value_set(elem: &mut ElemValue, idx: usize, val: u32) {
        alsa_sys::snd_ctl_elem_value_set_enumerated(
            elem.as_mut_ptr(),
            idx as c_uint,
            val as c_uint,
        );
    }

    /// Validate the u32 ElemValue.
    fn elem_value_validate(handle: &mut Ctl, id: &ElemId, val: u32) -> Result<()> {
        let info = ElemInfo::new(handle, id)?;
        if val >= info.items() {
            return Err(Error::InvalidEnumValue(
                id.name()?.to_owned(),
                val,
                info.items(),
            ));
        }
        Ok(())
    }

    /// Returns ElemType::Integer.
    fn elem_type() -> ElemType {
        ElemType::Enumerated
    }
}

impl CtlElemValue for u8 {
    type T = u8;
    /// Gets an u8 from the ElemValue.
    unsafe fn elem_value_get(elem: &ElemValue, idx: usize) -> u8 {
        alsa_sys::snd_ctl_elem_value_get_byte(elem.as_ptr(), idx as c_uint) as u8
    }
    /// Sets an u8 to the ElemValue.
    unsafe fn elem_value_set(elem: &mut ElemValue, idx: usize, val: u8) {
        alsa_sys::snd_ctl_elem_value_set_byte(elem.as_mut_ptr(), idx as c_uint, val as c_uchar);
    }
    /// Returns ElemType::Bytes.
    fn elem_type() -> ElemType {
        ElemType::Bytes
    }
}

/// All primitive types of a control element should implement `CtlElemValue` trait.
pub trait CtlElemValue {
    /// The primitive type of a control element.
    type T: Default + Clone + Copy + Debug;
    /// Gets the value from the ElemValue.
    #[allow(clippy::missing_safety_doc)]
    unsafe fn elem_value_get(value: &ElemValue, idx: usize) -> Self::T;
    /// Sets the value to the ElemValue.
    #[allow(clippy::missing_safety_doc)]
    unsafe fn elem_value_set(value: &mut ElemValue, id: usize, val: Self::T);
    /// Validate the input value.
    fn elem_value_validate(_handle: &mut Ctl, _id: &ElemId, _val: Self::T) -> Result<()> {
        Ok(())
    }
    /// Gets the data type itself can read and write.
    fn elem_type() -> ElemType;
}

/// Use `Elem` trait to access the underlying control element through the given `Ctl` and `ElemId`.
pub trait Elem: Sized {
    /// The data type of a control element.
    type T;
    /// Reads the value from the mixer control.
    fn load(handle: &mut Ctl, id: &ElemId) -> Result<Self::T>;
    /// Saves the value to the mixer control.
    fn save(handle: &mut Ctl, id: &ElemId, val: Self::T) -> Result<bool>;
    /// Gets the data type itself can read and write.
    fn elem_type() -> ElemType;
    /// Gets the number of value entries itself can read and write.
    fn size() -> usize;
}

impl<V: CtlEnumElemValue, const N: usize> EnumElem for [V; N] {}
/// Use `EnumElem` trait to present Enumerated control element.
pub trait EnumElem: Elem {}

/// Implements `EnumElem` for [u32; n] where n = 1 to 128.
pub trait CtlEnumElemValue: CtlElemValue {}
impl CtlEnumElemValue for u32 {}
