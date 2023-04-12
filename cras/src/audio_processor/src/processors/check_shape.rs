// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use std::marker::PhantomData;

use crate::AudioProcessor;
use crate::Error;
use crate::MultiSlice;
use crate::Result;
use crate::Sample;

/// `CheckShape` returns the input unmodified if the input matches the specified
/// shape. Otherwise an error is returned.
pub struct CheckShape<T: Sample> {
    channels: usize,
    block_size: usize,
    phantom: PhantomData<T>,
}

impl<T> CheckShape<T>
where
    T: Sample,
{
    pub fn new(channels: usize, block_size: usize) -> Self {
        Self {
            channels,
            block_size,
            phantom: PhantomData,
        }
    }
}

impl<T> AudioProcessor for CheckShape<T>
where
    T: Sample,
{
    type I = T;
    type O = T;

    fn process<'a>(&'a mut self, input: MultiSlice<'a, T>) -> Result<MultiSlice<'a, T>> {
        if input.min_len() == self.block_size && input.channels() == self.channels {
            Ok(input)
        } else {
            Err(Error::InvalidShape {
                want_channels: self.channels,
                want_frames: self.block_size,
                got_channels: input.channels(),
                got_frames: input.min_len(),
            })
        }
    }
}

#[cfg(test)]
mod tests {

    use crate::processors::CheckShape;
    use crate::AudioProcessor;
    use crate::MultiBuffer;

    #[test]
    fn good_shape() {
        let mut ap = CheckShape::<i32>::new(2, 4);
        let mut buf = MultiBuffer::from(vec![vec![1, 2, 3, 4], vec![5, 6, 7, 8]]);

        let result = ap.process(buf.as_multi_slice());
        assert_eq!(
            result.unwrap().into_raw(),
            vec![vec![1, 2, 3, 4], vec![5, 6, 7, 8]]
        );
    }

    #[test]
    fn bad_shape() {
        let mut ap = CheckShape::<i32>::new(2, 4);
        let mut buf = MultiBuffer::from(vec![vec![1, 2, 3], vec![4, 5, 6], vec![7, 8, 9]]);

        let result = ap.process(buf.as_multi_slice());
        let err = result.unwrap_err();
        assert_eq!(
            err.to_string(),
            "invalid channels x frames: want 2x4; got 3x3"
        );
    }
}
