// Copyright 2022 The ChromiumOS Authors.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::{slice_cast::SliceCast, Sample};

/// A `MultiBuffer` holds multiple buffers of audio samples or bytes.
/// Each buffer typically holds a channel of audio data.
pub struct MultiBuffer<T> {
    pub data: Vec<Vec<T>>,
}

impl<T> From<Vec<Vec<T>>> for MultiBuffer<T> {
    /// Take ownership from `vec` and create a `MultiBuffer`.
    fn from(vec: Vec<Vec<T>>) -> Self {
        Self { data: vec }
    }
}

impl<T: Clone> MultiBuffer<T> {
    /// Create `n` buffers, each with the specified `capacity`.
    pub fn with_capacity(capacity: usize, n: usize) -> Self {
        Self {
            data: vec![Vec::with_capacity(capacity); n],
        }
    }
}

impl<'a, S: Sample> MultiBuffer<S> {
    /// Get a [`MultiSlice`] referencing the `MultiBuffer`
    pub fn as_multi_slice(&'a mut self) -> MultiSlice<'a, S> {
        MultiSlice::from_vecs(&mut self.data)
    }

    /// Create `n` buffers with each with the specified `length`.
    /// Contents are initialized to `S::default()`.
    pub fn new(length: usize, n: usize) -> Self {
        Self::from(vec![vec![S::default(); length]; n])
    }

    /// Create `n` buffers with each with the specified `length`.
    /// Contents are initialized to `S::EQUILIBRIUM`.
    pub fn new_equilibrium(length: usize, n: usize) -> Self {
        Self::from(vec![vec![S::EQUILIBRIUM; length]; n])
    }
}

#[cfg(test)]
mod buffer_tests {
    use crate::MultiBuffer;

    #[test]
    fn new() {
        let buf = MultiBuffer::<f32>::new(4, 2);
        assert_eq!(buf.data, [[0., 0., 0., 0.], [0., 0., 0., 0.]]);

        let buf = MultiBuffer::<u8>::new(4, 2);
        assert_eq!(buf.data, [[0, 0, 0, 0], [0, 0, 0, 0]]);
    }

    #[test]
    fn new_equilibrium() {
        let buf = MultiBuffer::<f32>::new_equilibrium(4, 2);
        assert_eq!(buf.data, [[0., 0., 0., 0.], [0., 0., 0., 0.]]);

        let buf = MultiBuffer::<u8>::new_equilibrium(4, 2);
        assert_eq!(buf.data, [[128, 128, 128, 128], [128, 128, 128, 128]]);
    }

    #[test]
    fn with_capacity() {
        let buf = MultiBuffer::<f32>::with_capacity(4, 2);
        assert_eq!(buf.data, [[], []]);
    }

    #[test]
    fn as_multi_slice() {
        let mut buf = MultiBuffer::from(vec![vec![1, 2, 3, 4], vec![5, 6, 7, 8]]);
        let mut slices = buf.as_multi_slice();
        slices.data[0][1] = 0;
        slices.data[1][2] = 0;
        assert_eq!(buf.data, [[1, 0, 3, 4], [5, 6, 0, 8]]);
    }
}

/// A `MultiSlice` holds multiple references to buffers of audio samples or bytes.
/// Each slice typically references a channel of audio data.
pub struct MultiSlice<'a, T> {
    data: Vec<&'a mut [T]>,
}

impl<'a, T> MultiSlice<'a, T> {
    /// Create a `MultiSlice` referencing data owned by `vecs`.
    pub fn from_vecs(vecs: &'a mut [Vec<T>]) -> Self {
        Self {
            data: vecs.iter_mut().map(|ch| &mut ch[..]).collect(),
        }
    }

    /// Create a `MultiSlice` referencing data referenced by `slices`.
    pub fn from_raw(slices: Vec<&'a mut [T]>) -> Self {
        Self { data: slices }
    }

    /// Consume `self` and return the holded slices.
    pub fn into_raw(self) -> Vec<&'a mut [T]> {
        self.data
    }

    /// Convert to `MultiSlice<u8>`.
    /// This is a "view" conversion, the underlying memory is unchanged.
    pub fn into_bytes(self) -> MultiSlice<'a, u8>
    where
        T: Sample,
    {
        MultiSlice::<'a, u8> {
            data: self.data.into_iter().map(|slice| slice.cast()).collect(),
        }
    }
}

impl<'a> MultiSlice<'a, u8> {
    /// Convert the `MultiSlice<u8>` to `MultiSlice<T>`.
    /// This is a "view" conversion, the underlying memory is unchanged.
    /// Panics if the size or alignment is invalid for `T`.
    pub fn into_typed<T>(self) -> MultiSlice<'a, T>
    where
        T: Sample,
    {
        MultiSlice::<'a, T> {
            data: self.data.into_iter().map(|slice| slice.cast()).collect(),
        }
    }
}

#[cfg(test)]
mod slice_tests {
    use crate::{MultiBuffer, MultiSlice};

    #[test]
    fn from_vecs() {
        let mut vecs = vec![vec![1, 2, 3, 4], vec![5, 6, 7, 8]];
        let mut slices = MultiSlice::from_vecs(&mut vecs);
        slices.data[0][2] = 0;
        slices.data[1][3] = 0;
        assert_eq!(vecs, [[1, 2, 0, 4], [5, 6, 7, 0]]);
    }

    #[test]
    fn slices_convertion() {
        let mut a0 = [1, 2, 3, 4];
        let mut a1 = [5, 6, 7, 8];
        let raw = vec![&mut a0[..], &mut a1[..]];
        let mut slices = MultiSlice::from_raw(raw);
        slices.data[0][1] = 0;
        slices.data[1][2] = 0;
        assert_eq!(slices.into_raw(), [[1, 0, 3, 4], [5, 6, 0, 8]]);
    }

    #[test]
    fn into_typed() {
        let mut buf = MultiBuffer::from(vec![vec![0x11u8, 0, 0, 0x11, 0, 0x22, 0x22, 0]]);
        assert_eq!(
            buf.as_multi_slice().into_typed::<i32>().into_raw(),
            [[0x11000011i32, 0x00222200]]
        );
        assert_eq!(
            buf.as_multi_slice().into_typed::<f32>().into_raw(),
            [[1.009744e-28, 3.134604e-39]]
        );
    }

    #[test]
    fn from_typed() {
        let mut buf = MultiBuffer::from(vec![vec![0x11000011i32, 0x00222200]]);
        assert_eq!(
            buf.as_multi_slice().into_bytes().data,
            [[0x11u8, 0, 0, 0x11, 0, 0x22, 0x22, 0]]
        );

        let mut buf: MultiBuffer<f32> = MultiBuffer::from(vec![vec![1.009744e-28, 3.134604e-39]]);
        assert_eq!(
            buf.as_multi_slice().into_bytes().data,
            [[0x11u8, 0, 0, 0x11, 0, 0x22, 0x22, 0]]
        )
    }

    #[test]
    fn identity() {
        let numbers = vec![
            vec![-1f32, std::f32::consts::LN_2],
            vec![std::f32::consts::PI, f32::MAX],
        ];
        let mut buf = MultiBuffer::from(numbers.clone());
        assert_eq!(
            buf.as_multi_slice()
                .into_bytes()
                .into_typed::<f32>()
                .into_raw(),
            numbers
        );
    }
}
