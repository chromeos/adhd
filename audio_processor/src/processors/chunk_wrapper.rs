// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::AudioProcessor;
use crate::MultiBuffer;
use crate::Sample;
use crate::Shape;

/// The `ChunkWrapper` struct is an audio processor that wraps another audio
/// processor `T`. It processes audio data in chunks of a fixed size
/// specified by `block_size`.
pub struct ChunkWrapper<T: AudioProcessor<I = S, O = S>, S: Sample> {
    inner: T,
    block_size: usize,

    index: usize,
    pending: MultiBuffer<T::I>,
    processed: MultiBuffer<T::I>,
}

impl<T: AudioProcessor<I = S, O = S>, S: Sample> AudioProcessor for ChunkWrapper<T, S> {
    type I = S;
    type O = S;

    fn process<'a>(
        &'a mut self,
        mut input: crate::MultiSlice<'a, Self::I>,
    ) -> crate::Result<crate::MultiSlice<'a, Self::O>> {
        let mut remaining = input.min_len();
        let mut x = 0;

        // Process audio, when the remaining bytes overflows the pending buffer.
        while self.index + remaining >= self.block_size {
            // Number of frames to take from input.
            let n = self.block_size - self.index;

            self.exchange(self.index..self.index + n, &mut input.indexes(x..x + n));

            // Process the block, move index back to 0.
            self.processed
                .as_multi_slice()
                .clone_from_multi_slice(&self.inner.process(self.pending.as_multi_slice())?);
            self.index = 0;
            x += n;
            remaining -= n;
        }

        // Handle leftover input that are not processed.
        self.exchange(
            self.index..self.index + remaining,
            &mut input.indexes(x..x + remaining),
        );
        self.index += remaining;

        Ok(input)
    }
}

impl<T: AudioProcessor<I = S, O = S>, S: Sample> ChunkWrapper<T, S> {
    // Copy mslice to self.pending[range], then copy self.processed[range] to mslice.
    fn exchange(&mut self, range: std::ops::Range<usize>, mslice: &mut crate::MultiSlice<S>) {
        self.pending
            .as_multi_slice()
            .indexes(range.clone())
            .clone_from_multi_slice(mslice);
        mslice.clone_from_multi_slice(&self.processed.as_multi_slice().indexes(range));
    }
}

impl<T: AudioProcessor<I = S, O = S>, S: Sample> ChunkWrapper<T, S> {
    /// Create a new ChunkWrapper. `input_channels` and `output_channels`
    /// should match those of `inner`. They are allowed to be different
    /// to support down-mixing processors.
    pub fn new(inner: T, block_size: usize, input_channels: usize, output_channels: usize) -> Self {
        assert!(input_channels >= output_channels);
        ChunkWrapper {
            block_size,
            inner,
            index: 0,
            pending: MultiBuffer::new_equilibrium(Shape {
                channels: input_channels,
                frames: block_size,
            }),
            processed: MultiBuffer::new_equilibrium(Shape {
                channels: output_channels,
                frames: block_size,
            }),
        }
    }
}

#[cfg(test)]
mod tests {
    use super::ChunkWrapper;
    use crate::processors::InPlaceNegateAudioProcessor;
    use crate::processors::NegateAudioProcessor;
    use crate::AudioProcessor;
    use crate::MultiBuffer;

    // Test that ChunkedWrapper generates the correct delay.
    fn delayed_neg<T: AudioProcessor<I = i32, O = i32>>(neg: T) {
        let mut cw = ChunkWrapper::new(neg, 2, 2, 2);

        let mut input = MultiBuffer::from(vec![vec![1, 2, 3], vec![4, 5, 6]]);
        assert_eq!(
            cw.process(input.as_multi_slice()).unwrap().into_raw(),
            [[0i32, 0, -1], [0, 0, -4]]
        );

        let mut input = MultiBuffer::from(vec![vec![7, 8, 9, 10], vec![11, 12, 13, 14]]);
        assert_eq!(
            cw.process(input.as_multi_slice()).unwrap().into_raw(),
            [[-2i32, -3, -7, -8], [-5, -6, -11, -12]]
        );

        let mut input = MultiBuffer::from(vec![vec![15], vec![16]]);
        assert_eq!(
            cw.process(input.as_multi_slice()).unwrap().into_raw(),
            [[-9i32], [-13]]
        );
    }

    #[test]
    fn delayed_neg_test() {
        delayed_neg(NegateAudioProcessor::<i32>::new(2, 2));
    }

    #[test]
    fn delayed_neg_in_place_test() {
        delayed_neg(InPlaceNegateAudioProcessor::<i32>::new());
    }

    // TODO: Add a test for when input_channels and output_channels are different.

    // TODO: Add mock-based testing to check that data passed into
    // ChunkWrapper.inner is correct.
}
