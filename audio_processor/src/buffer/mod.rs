// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use std::ops::Index;
use std::ops::IndexMut;

use crate::slice_cast::SliceCast;
use crate::Sample;
use crate::Shape;

mod debug;

/// A `MultiBuffer` holds multiple buffers of audio samples or bytes.
/// Each buffer typically holds a channel of audio data.
pub struct MultiBuffer<T> {
    shape: Shape,
    // Deinterleaved audio data.
    buffer: Vec<T>,
}

impl<T> Index<usize> for MultiBuffer<T> {
    type Output = [T];

    fn index(&self, index: usize) -> &Self::Output {
        if index >= self.shape.channels {
            panic!("index {} >= {}", index, self.shape.channels);
        }
        let start = index * self.shape.frames;
        &self.buffer[start..start + self.shape.frames]
    }
}

impl<T> IndexMut<usize> for MultiBuffer<T> {
    fn index_mut(&mut self, index: usize) -> &mut Self::Output {
        if index >= self.shape.channels {
            panic!("index {} >= {}", index, self.shape.channels);
        }
        let start = index * self.shape.frames;
        &mut self.buffer[start..start + self.shape.frames]
    }
}

impl<T> From<Vec<Vec<T>>> for MultiBuffer<T> {
    /// Take ownership from `vec` and create a `MultiBuffer`.
    fn from(vec: Vec<Vec<T>>) -> Self {
        let min_len = vec.iter().map(|ch| ch.len()).min().unwrap_or(0);
        let max_len = vec.iter().map(|ch| ch.len()).max().unwrap_or(0);
        assert_eq!(min_len, max_len);
        Self {
            shape: Shape {
                channels: vec.len(),
                frames: min_len,
            },
            buffer: vec.into_iter().flatten().collect::<Vec<T>>(),
        }
    }
}

impl<T: Clone> MultiBuffer<T> {
    pub fn to_vecs(&self) -> Vec<Vec<T>> {
        (0..self.shape.channels)
            .map(|ch| self[ch].to_vec())
            .collect()
    }
}

impl<'a, T> From<MultiSlice<'a, T>> for MultiBuffer<T>
where
    T: Sample,
{
    fn from(s: MultiSlice<'a, T>) -> Self {
        let v: Vec<Vec<T>> = s.iter().map(|ch| ch.to_vec()).collect();
        Self::from(v)
    }
}

impl<'a, S: Sample> MultiBuffer<S> {
    /// Get a [`MultiSlice`] referencing the `MultiBuffer`
    pub fn as_multi_slice(&'a mut self) -> MultiSlice<'a, S> {
        if self.buffer.len() == 0 {
            // `Chunks` and `ChunksMut` don't work on empty.
            MultiSlice::from_slices((0..self.shape.channels).map(|_| &mut [][..]))
        } else {
            MultiSlice::from_slices(self.buffer.chunks_mut(self.shape.frames))
        }
    }

    /// Create `shape.channels` buffers with each with `shape.frames` samples.
    /// Contents are initialized to `S::default()`.
    pub fn new(shape: Shape) -> Self {
        Self::from(vec![vec![S::default(); shape.frames]; shape.channels])
    }

    /// Create `shape.channels` buffers with each with `shape.frames` samples.
    /// Contents are initialized to `S::EQUILIBRIUM`.
    pub fn new_equilibrium(shape: Shape) -> Self {
        Self::from(vec![vec![S::EQUILIBRIUM; shape.frames]; shape.channels])
    }
}

#[cfg(test)]
mod buffer_tests {
    use crate::MultiBuffer;
    use crate::Shape;

    #[test]
    fn new() {
        let buf = MultiBuffer::<f32>::new(Shape {
            channels: 2,
            frames: 4,
        });
        assert_eq!(buf.to_vecs(), [[0., 0., 0., 0.], [0., 0., 0., 0.]]);

        let buf = MultiBuffer::<u8>::new(Shape {
            channels: 2,
            frames: 4,
        });
        assert_eq!(buf.to_vecs(), [[0, 0, 0, 0], [0, 0, 0, 0]]);
    }

    #[test]
    fn new_equilibrium() {
        let buf = MultiBuffer::<f32>::new_equilibrium(Shape {
            channels: 2,
            frames: 4,
        });
        assert_eq!(buf.to_vecs(), [[0., 0., 0., 0.], [0., 0., 0., 0.]]);

        let buf = MultiBuffer::<u8>::new_equilibrium(Shape {
            channels: 2,
            frames: 4,
        });
        assert_eq!(buf.to_vecs(), [[128, 128, 128, 128], [128, 128, 128, 128]]);
    }

    #[test]
    fn as_multi_slice() {
        let mut buf = MultiBuffer::from(vec![vec![1, 2, 3, 4], vec![5, 6, 7, 8]]);
        let mut slices = buf.as_multi_slice();
        slices.data[0][1] = 0;
        slices.data[1][2] = 0;
        assert_eq!(buf.to_vecs(), [[1, 0, 3, 4], [5, 6, 0, 8]]);
    }
}

/// A `MultiSlice` holds multiple references to buffers of audio samples or bytes.
/// Each slice typically references a channel of audio data.
pub struct MultiSlice<'a, T> {
    data: Vec<&'a mut [T]>,
}

impl<'a, T> MultiSlice<'a, T> {
    /// Create a `MultiSlice` referencing data owned by `slices`.
    pub fn from_slices<I>(slices: I) -> Self
    where
        I: IntoIterator<Item = &'a mut [T]>,
    {
        Self {
            data: slices.into_iter().collect(),
        }
    }

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

    /// Returns an iterator over the slices.
    pub fn iter(&self) -> std::slice::Iter<&mut [T]> {
        self.data.iter()
    }

    /// Returns an iterator over the mutable slices.
    pub fn iter_mut(&mut self) -> std::slice::IterMut<&'a mut [T]> {
        self.data.iter_mut()
    }

    /// Returns the number of slices
    pub fn channels(&self) -> usize {
        self.data.len()
    }

    /// Returns the smallest len of the contained slices.
    pub fn min_len(&self) -> usize {
        self.data
            .iter()
            .map(|buf| buf.len())
            .min()
            .unwrap_or_default()
    }

    /// Returns a `MultiSlice` referencing `slice[range]` for each contained slice.
    pub fn indexes(&mut self, range: std::ops::Range<usize>) -> MultiSlice<'_, T> {
        MultiSlice::from_raw(
            self.data
                .iter_mut()
                .map(|ch| &mut ch[range.clone()])
                .collect(),
        )
    }

    /// Consume self and return a `MultiSlice` referencing `slice[range]` for each contained slice.
    pub fn into_indexes(self, range: std::ops::Range<usize>) -> Self {
        MultiSlice::from_raw(
            self.data
                .into_iter()
                .map(|ch| &mut ch[range.clone()])
                .collect(),
        )
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

impl<'a, T: Clone> MultiSlice<'a, T> {
    pub fn clone_from_multi_slice(&mut self, src: &MultiSlice<T>) {
        for (to, from) in self.iter_mut().zip(src.iter()) {
            to.clone_from_slice(from)
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
    use crate::MultiBuffer;
    use crate::MultiSlice;

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

    #[test]
    fn iter() {
        let ch0 = vec![1i32, 2, 3];
        let ch1 = vec![4i32, 5, 6, 7];
        let mut buf = vec![ch0.clone(), ch1.clone()];
        let slices = MultiSlice::from_vecs(&mut buf);
        let mut it = slices.iter();
        assert_eq!(*it.next().unwrap(), ch0);
        assert_eq!(*it.next().unwrap(), ch1);
        assert!(it.next().is_none());
    }

    #[test]
    fn iter_mut() {
        let mut buf = vec![vec![1i32, 2, 3], vec![4i32, 5, 6, 7]];
        let mut slices = MultiSlice::from_vecs(&mut buf);
        let mut it = slices.iter_mut();

        let ch0 = it.next().unwrap();
        assert_eq!(*ch0, [1, 2, 3]);
        ch0[0] = 0;

        let ch1 = it.next().unwrap();
        assert_eq!(*ch1, [4, 5, 6, 7]);
        ch1[1] = 0;

        assert!(it.next().is_none());

        assert_eq!(buf, vec![vec![0, 2, 3], vec![4, 0, 6, 7]]);
    }

    #[test]
    fn min_len() {
        let mut buf = vec![vec![1, 2, 3, 4], vec![5, 6, 7], vec![8, 9, 10, 11, 12]];
        assert_eq!(MultiSlice::from_vecs(&mut buf).min_len(), 3);
    }

    #[test]
    fn indexes() {
        let mut buf = MultiBuffer::from(vec![vec![1, 2, 3, 4], vec![5, 6, 7, 8]]);

        assert_eq!(
            buf.as_multi_slice().indexes(0..2).into_raw(),
            [[1, 2], [5, 6]]
        );
        assert_eq!(
            buf.as_multi_slice().indexes(2..4).into_raw(),
            [[3, 4], [7, 8]]
        );

        for ch in buf.as_multi_slice().indexes(1..3).iter_mut() {
            for x in ch.iter_mut() {
                *x = 0;
            }
        }
        assert_eq!(buf.to_vecs(), [[1, 0, 0, 4], [5, 0, 0, 8]]);
    }

    #[test]
    fn into_indexes() {
        let mut buf = MultiBuffer::from(vec![vec![1, 2, 3, 4], vec![5, 6, 7, 8]]);

        assert_eq!(
            buf.as_multi_slice().into_indexes(0..2).into_raw(),
            [[1, 2], [5, 6]]
        );
        assert_eq!(
            buf.as_multi_slice().into_indexes(2..4).into_raw(),
            [[3, 4], [7, 8]]
        );

        for ch in buf.as_multi_slice().into_indexes(1..3).iter_mut() {
            for x in ch.iter_mut() {
                *x = 0;
            }
        }
        assert_eq!(buf.to_vecs(), [[1, 0, 0, 4], [5, 0, 0, 8]]);
    }
}
