// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use super::binding;
use super::PluginError;
use crate::AudioProcessor;

pub type PluginProcessorCreate = unsafe extern "C" fn(
    out: *mut *mut binding::plugin_processor,
    config: *const binding::plugin_processor_config,
) -> binding::status;

pub type PluginProcessorRun = unsafe extern "C" fn(
    *mut binding::plugin_processor,
    *const binding::multi_slice,
    *mut binding::multi_slice,
) -> binding::status;

pub type PluginProcessorDestroy =
    unsafe extern "C" fn(*mut binding::plugin_processor) -> binding::status;

#[derive(Debug)]
/// `PluginProcessor` is an [`AudioProcessor`] that wraps an audio processor
/// implemented in another language.
pub struct PluginProcessor {
    handle: *mut binding::plugin_processor,
    run: PluginProcessorRun,
    destroy: PluginProcessorDestroy,
}

/// Helper to create a [`crate::Error`] for unexpected NULLs.
fn null_error(desc: &str) -> crate::Error {
    crate::Error::Plugin(PluginError::UnexpectedNull(String::from(desc)))
}

impl PluginProcessor {
    /// Create a new [`PluginProcessor`] from the [`PluginProcessorCreate`]
    /// constructor.
    ///
    /// # Safety
    /// This function is safe only if the `constructor` is a pointer to
    /// a `plugin_processor_create` function. See also `plugin_processor.h`.
    pub unsafe fn new(
        constructor: PluginProcessorCreate,
        block_size: usize,
        channels: usize,
        frame_rate: usize,
    ) -> crate::Result<PluginProcessor> {
        let config = binding::plugin_processor_config {
            block_size,
            channels,
            frame_rate,
            debug: false,
        };

        let mut handle = std::mem::MaybeUninit::zeroed();

        constructor(handle.as_mut_ptr(), &config).check()?;
        let handle = handle.assume_init();
        if handle.is_null() {
            return Err(null_error("create"));
        }

        Self::from_handle(handle)
    }

    /// Create a [`PluginProcessor`] from a already created plugin_processor
    /// C struct.
    ///
    /// If the processor cannot be successfully constructed, an error is
    /// returned and the handle is destroyed with best-effort.
    ///
    /// # Safety
    ///
    /// The safety depends on the C side to properly implement the
    /// plugin_processor.h API.
    pub unsafe fn from_handle(
        handle: *mut binding::plugin_processor,
    ) -> crate::Result<PluginProcessor> {
        let ops = (*handle).ops.as_ref().ok_or_else(|| null_error("ops"))?;
        let destroy = ops.destroy.ok_or_else(|| null_error("ops.destroy"))?;
        let run = ops.run.ok_or_else(|| {
            destroy(handle);
            null_error("ops.run")
        })?;

        Ok(PluginProcessor {
            handle,
            run,
            destroy,
        })
    }
}

impl Drop for PluginProcessor {
    fn drop(&mut self) {
        // SAFETY: If the processor constructs successfully we assume it is
        // safe to call its methods.
        unsafe { (self.destroy)(self.handle) }
            .check()
            .expect("cannot destruct processor");
    }
}

impl AudioProcessor for PluginProcessor {
    type I = f32;
    type O = f32;

    fn process<'a>(
        &'a mut self,
        input: crate::MultiSlice<'a, Self::I>,
    ) -> crate::Result<crate::MultiSlice<'a, Self::O>> {
        let mut c_input = binding::multi_slice::try_from(input)?;
        let mut c_output = binding::multi_slice::zeroed();
        // SAFETY: we assume that `self` is a valid `PluginProcessor`, thus
        // calling `self.run` is safe.
        unsafe {
            (self.run)(self.handle, &mut c_input, &mut c_output).check()?;
        }
        // SAFETY: We assume that `self.run` returns a valid multi_slice.
        let output = unsafe { crate::MultiSlice::<f32>::from_raw(c_output.as_slice_vec()) };
        Ok(output)
    }
}

#[cfg(feature = "bazel")]
#[cfg(test)]
mod plugin_tests {
    use assert_matches::assert_matches;

    use super::binding;
    use super::PluginError;
    use super::PluginProcessor;
    use crate::AudioProcessor;
    use crate::MultiBuffer;
    use crate::Shape;

    #[test]
    fn oom_create() {
        let err = unsafe { PluginProcessor::new(binding::bad_plugin_oom_create, 480, 2, 48000) }
            .unwrap_err();
        assert_matches!(err, crate::Error::Plugin(PluginError::Binding(_)));
    }

    #[test]
    fn null_processor() {
        let err = unsafe {
            PluginProcessor::new(binding::bad_plugin_null_processor_create, 480, 2, 48000)
        }
        .unwrap_err();
        assert_matches!(err, crate::Error::Plugin(PluginError::UnexpectedNull(_)));
    }

    #[test]
    fn null_ops() {
        let err =
            unsafe { PluginProcessor::new(binding::bad_plugin_null_ops_create, 480, 2, 48000) }
                .unwrap_err();
        assert_matches!(err, crate::Error::Plugin(PluginError::UnexpectedNull(_)));
    }

    #[test]
    fn missing_run() {
        let err =
            unsafe { PluginProcessor::new(binding::bad_plugin_missing_run_create, 480, 2, 48000) }
                .unwrap_err();
        assert_matches!(err, crate::Error::Plugin(PluginError::UnexpectedNull(_)));
    }

    #[test]
    fn missing_destroy() {
        let err = unsafe {
            PluginProcessor::new(binding::bad_plugin_missing_destroy_create, 480, 2, 48000)
        }
        .unwrap_err();
        assert_matches!(err, crate::Error::Plugin(PluginError::UnexpectedNull(_)));
    }

    #[test]
    fn failing_run() {
        let mut p =
            unsafe { PluginProcessor::new(binding::bad_plugin_failing_run_create, 480, 2, 48000) }
                .unwrap();
        let mut buf = MultiBuffer::<f32>::new_equilibrium(Shape {
            channels: 2,
            frames: 480,
        });
        let err = p.process(buf.as_multi_slice()).unwrap_err();
        assert_matches!(err, crate::Error::Plugin(PluginError::Binding(_)));
    }

    #[test]
    fn negate_process() {
        let mut input: MultiBuffer<f32> =
            MultiBuffer::from(vec![vec![1., 2., 3., 4.], vec![5., 6., 7., 8.]]);
        let mut ap =
            unsafe { PluginProcessor::new(binding::negate_processor_create, 4, 2, 48000) }.unwrap();

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
