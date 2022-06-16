// Copyright 2022 The ChromiumOS Authors.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::Sample;

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
mod tests {
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
}
