// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use super::dl;
use super::PluginProcessor;
use super::PluginProcessorCreate;
use crate::AudioProcessor;

/// `DynamicPluginProcessor` supports running [`PluginProcessor`]s in dynamic
/// loaded libraries.
#[derive(Debug)]
pub struct DynamicPluginProcessor {
    processor: PluginProcessor,

    // The library has to be dropped last.
    // https://doc.rust-lang.org/std/ops/trait.Drop.html#drop-order
    _library: dl::DynLib,
}

impl DynamicPluginProcessor {
    /// Create a new `DynamicPluginProcessor` from dynamically loaded `lib`
    /// and a constructor named `symbol`.
    pub fn new(
        lib: &str,
        symbol: &str,
        block_size: usize,
        channels: usize,
        frame_rate: usize,
    ) -> crate::Result<Self> {
        let dyn_lib = dl::DynLib::new(lib)?;
        let constructor: PluginProcessorCreate =
            unsafe { std::mem::transmute(dyn_lib.sym(symbol)?) };

        // SAFETY: We assume that the dynamic library is safe if it is
        // loaded successfully.
        let processor =
            unsafe { PluginProcessor::new(constructor, block_size, channels, frame_rate) }?;

        Ok(Self {
            processor,
            _library: dyn_lib,
        })
    }
}

impl AudioProcessor for DynamicPluginProcessor {
    type I = f32;
    type O = f32;

    fn process<'a>(
        &'a mut self,
        input: crate::MultiSlice<'a, Self::I>,
    ) -> crate::Result<crate::MultiSlice<'a, Self::O>> {
        self.processor.process(input)
    }
}

#[cfg(feature = "bazel")]
#[cfg(test)]
mod tests {
    use std::env;

    use assert_matches::assert_matches;

    use crate::processors::DynamicPluginProcessor;
    use crate::processors::PluginError;
    use crate::AudioProcessor;
    use crate::MultiBuffer;

    fn dl_lib_path() -> String {
        env::var("LIBTEST_PLUGINS_SO").unwrap()
    }

    #[test]
    fn dlopen_error() {
        let err = DynamicPluginProcessor::new("./does_not_exist.so", "ctor", 1, 1, 1).unwrap_err();
        assert_matches!(err, crate::Error::Plugin(PluginError::Dl { .. }));
    }

    #[test]
    fn dlsym_error() {
        let err =
            DynamicPluginProcessor::new(&dl_lib_path(), "does_not_exist", 1, 1, 1).unwrap_err();
        assert_matches!(err, crate::Error::Plugin(PluginError::Dl { .. }));
    }

    #[test]
    fn negate_process() {
        let mut input: MultiBuffer<f32> =
            MultiBuffer::from(vec![vec![1., 2., 3., 4.], vec![5., 6., 7., 8.]]);
        let mut ap =
            DynamicPluginProcessor::new(&dl_lib_path(), "negate_processor_create", 4, 2, 48000)
                .unwrap();

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
