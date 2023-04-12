// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![allow(non_upper_case_globals)]
#![allow(non_camel_case_types)]
#![allow(non_snake_case)]

use std::slice;

use super::PluginError;

include!(concat!(env!("OUT_DIR"), "/plugin_processor_binding.rs"));

impl std::fmt::Display for status {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        match *self {
            status::StatusOk => write!(f, "ok"),
            status::ErrInvalidProcessor => write!(f, "invalid processor"),
            status::ErrOutOfMemory => write!(f, "out of memory"),
            status::ErrInvalidConfig => write!(f, "invalid config"),
            status::ErrInvalidArgument => write!(f, "invalid argument"),
            other => write!(f, "unknown plugin processor error code {}", other.0),
        }
    }
}

impl status {
    pub(super) fn check(self) -> Result<(), super::PluginError> {
        match self {
            status::StatusOk => Ok(()),
            _ => Err(super::PluginError::Binding(self)),
        }
    }
}

impl multi_slice {
    /// Return a zeroed `multi_slice`.
    pub fn zeroed() -> Self {
        Self {
            channels: 0,
            num_frames: 0,
            data: [std::ptr::null_mut(); MULTI_SLICE_MAX_CH as usize],
        }
    }

    /// Return a vector of slices to the data.
    ///
    /// # Safety
    ///
    /// The caller must ensure that the first `self.channels` pointers
    /// in `self.data` point to `self.num_frames` slices.
    pub unsafe fn as_slice_vec<'a>(&mut self) -> Vec<&'a mut [f32]> {
        self.data[..self.channels as usize]
            .iter_mut()
            .map(|&mut ptr| slice::from_raw_parts_mut(ptr, self.num_frames as usize))
            .collect()
    }
}

impl TryFrom<crate::MultiSlice<'_, f32>> for multi_slice {
    type Error = PluginError;

    fn try_from(mut value: crate::MultiSlice<'_, f32>) -> Result<Self, Self::Error> {
        let mut vec: Vec<*mut f32> = value.iter_mut().map(|ch| ch.as_mut_ptr()).collect();
        let channels = vec.len();
        if channels > MULTI_SLICE_MAX_CH as usize {
            return Err(PluginError::TooManyChannels(channels));
        }
        vec.resize(MULTI_SLICE_MAX_CH as usize, std::ptr::null_mut());
        let num_frames = value.min_len();

        Ok(multi_slice {
            channels,
            num_frames,
            data: vec
                .try_into()
                .expect("should never fail because vec is already resized"),
        })
    }
}

#[cfg(test)]
mod tests {
    use assert_matches::assert_matches;

    use super::multi_slice;
    use crate::processors::PluginError;
    use crate::MultiBuffer;

    #[test]
    fn as_slice_vec() {
        let mut bufs = [[1f32, 2., 3., 4.], [5., 6., 7., 8.]];

        let mut ms = multi_slice {
            channels: 2,
            num_frames: 4,
            data: [
                bufs[0].as_mut_ptr(),
                bufs[1].as_mut_ptr(),
                std::ptr::null_mut(),
                std::ptr::null_mut(),
                std::ptr::null_mut(),
                std::ptr::null_mut(),
                std::ptr::null_mut(),
                std::ptr::null_mut(),
            ],
        };

        let sv = unsafe { ms.as_slice_vec() };
        assert_eq!(sv, bufs);
    }

    #[test]
    fn try_from_multi_slice_too_many_channels() {
        let mut buf = MultiBuffer::<f32>::new_equilibrium(crate::Shape {
            channels: 100,
            frames: 480,
        });
        let ms = multi_slice::try_from(buf.as_multi_slice());
        assert_matches!(ms, Err(PluginError::TooManyChannels(100)));
    }
}
