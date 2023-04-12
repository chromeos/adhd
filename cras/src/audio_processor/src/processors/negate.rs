// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use std::marker::PhantomData;

use crate::AudioProcessor;
use crate::MultiBuffer;
use crate::MultiSlice;
use crate::Result;
use crate::Shape;
use crate::SignedSample;

/// `InPlaceNegateAudioProcessor` is an [`AudioProcessor`] that negates the audio samples.
/// Audio samples are modified in-place.
pub struct InPlaceNegateAudioProcessor<T: SignedSample> {
    phantom: PhantomData<T>,
}

impl<T: SignedSample> InPlaceNegateAudioProcessor<T> {
    /// Create a `InPlaceNegateAudioProcessor`.
    pub fn new() -> Self {
        Self {
            phantom: PhantomData,
        }
    }
}

impl<T: SignedSample> Default for InPlaceNegateAudioProcessor<T> {
    fn default() -> Self {
        Self::new()
    }
}

impl<T: SignedSample> AudioProcessor for InPlaceNegateAudioProcessor<T> {
    type I = T;
    type O = T;

    fn process<'a>(&'a mut self, mut input: MultiSlice<'a, T>) -> Result<MultiSlice<'a, T>> {
        for channel in input.iter_mut() {
            for x in channel.iter_mut() {
                *x = -*x;
            }
        }

        Ok(input)
    }
}

/// `NegateAudioProcessor` is an [`AudioProcessor`] that negates the audio samples.
/// Results are written to a dedicated output buffer and the input is unmodified.
pub struct NegateAudioProcessor<T: SignedSample> {
    output: MultiBuffer<T>,
}

impl<T: SignedSample> NegateAudioProcessor<T> {
    /// Create a `NegateAudioProcessor` with the specified parameters.
    ///
    /// Each [`MultiSlice`] passed to the [`AudioProcessor::process()`] function
    /// must have the matching `num_channels` slices and each slice must be
    ///  `num_frames` long.
    pub fn new(num_channels: usize, num_frames: usize) -> Self {
        Self {
            output: MultiBuffer::new(Shape {
                channels: num_channels,
                frames: num_frames,
            }),
        }
    }
}

impl<T: SignedSample> AudioProcessor for NegateAudioProcessor<T> {
    type I = T;
    type O = T;

    fn process<'a>(&'a mut self, input: MultiSlice<'a, T>) -> Result<MultiSlice<'a, T>> {
        for (inch, outch) in input.iter().zip(self.output.as_multi_slice().iter_mut()) {
            for (x, y) in inch.iter().zip(outch.iter_mut()) {
                *y = -*x;
            }
        }

        Ok(self.output.as_multi_slice())
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn in_place_process() {
        let mut input: MultiBuffer<f32> =
            MultiBuffer::from(vec![vec![1., 2., 3., 4.], vec![5., 6., 7., 8.]]);
        let mut ap: InPlaceNegateAudioProcessor<f32> = Default::default();

        let output = ap.process(input.as_multi_slice()).unwrap();

        // output = -input
        assert_eq!(
            output.into_raw(),
            [[-1., -2., -3., -4.], [-5., -6., -7., -8.]]
        );

        // in-place: input = -input
        assert_eq!(input.data, [[-1., -2., -3., -4.], [-5., -6., -7., -8.]]);
    }

    #[test]
    fn process() {
        let mut input: MultiBuffer<f32> =
            MultiBuffer::from(vec![vec![1., 2., 3., 4.], vec![5., 6., 7., 8.]]);
        let mut ap = NegateAudioProcessor::new(2, 4);

        let output = ap.process(input.as_multi_slice()).unwrap();

        // output = -input
        assert_eq!(
            output.into_raw(),
            [[-1., -2., -3., -4.], [-5., -6., -7., -8.]]
        );

        // non-in-place: input does not change
        assert_eq!(input.data, [[1., 2., 3., 4.], [5., 6., 7., 8.]]);
    }
}
