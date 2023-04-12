// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::AudioProcessor;
use crate::MultiBuffer;
use crate::Sample;
use crate::Shape;

/// Naive audio resampler using the nearest neighbor algorithm.
pub struct NearestNeighborResampler<T: Sample> {
    buffer: MultiBuffer<T>,
    indexes: Vec<usize>,
}

impl<T: Sample> AudioProcessor for NearestNeighborResampler<T> {
    type I = T;
    type O = T;

    fn process<'a>(
        &'a mut self,
        input: crate::MultiSlice<'a, Self::I>,
    ) -> crate::Result<crate::MultiSlice<'a, Self::O>> {
        for (oslice, islice) in self.buffer.as_multi_slice().iter_mut().zip(input.iter()) {
            for (&i, sample) in self.indexes.iter().zip(oslice.iter_mut()) {
                *sample = islice[i];
            }
        }

        Ok(self.buffer.as_multi_slice())
    }
}

impl<T: Sample> NearestNeighborResampler<T> {
    pub fn new(
        channels: usize,
        input_block_size: usize,
        output_block_size: usize,
    ) -> NearestNeighborResampler<T> {
        Self {
            buffer: MultiBuffer::new(Shape {
                frames: output_block_size,
                channels,
            }),
            indexes: build_index(input_block_size, output_block_size),
        }
    }
}

fn build_index(inputs: usize, outputs: usize) -> Vec<usize> {
    let mut i = 1;
    let indexes: Vec<usize> = (1..=outputs)
        .map(|o| {
            while i * outputs < o * inputs {
                i += 1
            }
            i - 1
        })
        .collect();

    debug_assert!(
        *indexes.last().unwrap() < inputs,
        "index {} out of bounds {:?}",
        indexes.last().unwrap(),
        0..inputs
    );

    indexes
}

#[cfg(test)]
mod tests {
    use super::build_index;
    use super::NearestNeighborResampler;
    use crate::AudioProcessor;
    use crate::MultiBuffer;

    #[test]
    fn three_to_one() {
        let mut input = MultiBuffer::from(vec![(1i32..=9).collect()]);
        let mut resampler = NearestNeighborResampler::<i32>::new(1, 9, 3);
        let output = resampler.process(input.as_multi_slice()).unwrap();
        assert_eq!(output.into_raw(), [[3, 6, 9]]);
    }

    #[test]
    fn one_to_three() {
        let mut input = MultiBuffer::from(vec![vec![1i32, 2, 3]]);
        let mut resampler = NearestNeighborResampler::<i32>::new(1, 3, 9);
        let output = resampler.process(input.as_multi_slice()).unwrap();
        assert_eq!(output.into_raw(), [[1, 1, 1, 2, 2, 2, 3, 3, 3]]);
    }

    #[test]
    fn multi_channel() {
        let mut input = MultiBuffer::from(vec![vec![1, 2, 3], vec![4, 5, 6]]);
        let mut resampler = NearestNeighborResampler::<i32>::new(2, 3, 6);
        let output = resampler.process(input.as_multi_slice()).unwrap();
        assert_eq!(
            output.into_raw(),
            [[1, 1, 2, 2, 3, 3,], [4, 4, 5, 5, 6, 6,]]
        );
    }

    #[test]
    fn indexes() {
        assert_eq!(build_index(1, 3), [0, 0, 0]);
        assert_eq!(build_index(3, 9), [0, 0, 0, 1, 1, 1, 2, 2, 2]);
        assert_eq!(build_index(9, 3), [2, 5, 8]);
        assert_eq!(build_index(3, 7), [0, 0, 1, 1, 2, 2, 2]);
        assert_eq!(build_index(7, 3), [2, 4, 6]);
    }
}
