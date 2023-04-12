// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::MultiSlice;
use crate::Sample;

#[derive(thiserror::Error, Debug)]
pub enum Error {
    #[error(
        "{}: want {want_channels}x{want_frames}; got {got_channels}x{got_frames}",
        "invalid channels x frames"
    )]
    InvalidShape {
        want_channels: usize,
        want_frames: usize,
        got_channels: usize,
        got_frames: usize,
    },
    #[error("error in hound: {0:?}")]
    Wav(hound::Error),
    #[error("error in plugin: {0}")]
    Plugin(#[from] crate::processors::PluginError),
}

pub type Result<T> = std::result::Result<T, Error>;

/// A ByteProcessor processes multiple slices of bytes.
/// Each iteration the `process_bytes` function is called.
pub trait ByteProcessor {
    /// Process audio pointed by `input`. Return the result.
    ///
    /// To implement an in-place processor, modify the input directly and return
    /// it.
    ///
    /// To implement an non in-place processor, store the output on memory owned
    /// by the processor itself, then return a [`MultiSlice`] referencing the memory
    /// owned by the processor.
    fn process_bytes<'a>(&'a mut self, input: MultiSlice<'a, u8>) -> Result<MultiSlice<'a, u8>>;
}

/// Convinence trait to ease implementing [`ByteProcessor`]s.
/// The input and output types are casted from `u8` to `I`, and `O` to `u8` automatically.
///
/// Prefer implementing [`AudioProcessor`] when the input and output types are
/// known at compile time. Otherwise implement [`ByteProcessor`].
pub trait AudioProcessor {
    /// Input type.
    type I: Sample;
    /// Output type.
    type O: Sample;

    /// Process audio pointed by `input`. Return the result.
    /// See also [`ByteProcessor::process_bytes`].
    fn process<'a>(&'a mut self, input: MultiSlice<'a, Self::I>)
        -> Result<MultiSlice<'a, Self::O>>;

    fn process_bytes<'a>(&'a mut self, input: MultiSlice<'a, u8>) -> Result<MultiSlice<'a, u8>> {
        self.process(input.into_typed()).map(|x| x.into_bytes())
    }
}

impl<T> ByteProcessor for T
where
    T: AudioProcessor,
{
    fn process_bytes<'a>(&'a mut self, input: MultiSlice<'a, u8>) -> Result<MultiSlice<'a, u8>> {
        self.process_bytes(input)
    }
}

#[cfg(test)]
mod tests {
    use crate::processors;
    use crate::ByteProcessor;
    use crate::MultiBuffer;

    #[test]
    fn simple_pipeline() {
        // Test a simple pipeline using a Vec of ByteProcessor.
        let mut p1 = processors::InPlaceNegateAudioProcessor::<f32>::new();
        let mut p2 = processors::NegateAudioProcessor::<f32>::new(2, 4);
        let mut pipeline: Vec<&mut dyn ByteProcessor> = vec![&mut p1, &mut p2];

        let mut bufs = MultiBuffer::<f32>::from(vec![vec![1., 2., 3., 4.], vec![5., 6., 7., 8.]]);
        let mut slices = bufs.as_multi_slice().into_bytes();

        for p in pipeline.iter_mut() {
            slices = p.process_bytes(slices).unwrap();
        }

        // Y = -(-X) = X
        assert_eq!(
            slices.into_typed::<f32>().into_raw(),
            [[1., 2., 3., 4.], [5., 6., 7., 8.]]
        );

        // Y = -X; p2 does not modify the input data
        assert_eq!(bufs.data, [[-1., -2., -3., -4.], [-5., -6., -7., -8.]]);
    }
}
