// Copyright 2022 The ChromiumOS Authors.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::{MultiSlice, Sample};

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
    fn process_bytes<'a>(&'a mut self, input: MultiSlice<'a, u8>) -> MultiSlice<'a, u8>;
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
    fn process<'a>(&'a mut self, input: MultiSlice<'a, Self::I>) -> MultiSlice<'a, Self::O>;

    fn process_bytes<'a>(&'a mut self, input: MultiSlice<'a, u8>) -> MultiSlice<'a, u8> {
        self.process(input.into_typed()).into_bytes()
    }
}

impl<T> ByteProcessor for T
where
    T: AudioProcessor,
{
    fn process_bytes<'a>(&'a mut self, input: MultiSlice<'a, u8>) -> MultiSlice<'a, u8> {
        self.process_bytes(input)
    }
}
