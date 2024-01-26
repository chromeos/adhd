// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use std::slice;

use super::binding;
use crate::AudioProcessor;
use crate::MultiSlice;

/// Export the [`AudioProcessor`] as a FFI plugin.
pub fn export_plugin(p: impl AudioProcessor<I = f32, O = f32>) -> *mut binding::plugin_processor {
    Exporter::new(p).export()
}

#[repr(C)]
struct Exporter<'a> {
    pub base: binding::plugin_processor,
    processor: Box<dyn AudioProcessor<I = f32, O = f32> + 'a>,
    frame_rate: usize,
}

impl<'a> Exporter<'a> {
    pub fn new(p: impl AudioProcessor<I = f32, O = f32> + 'a) -> Self {
        let frame_rate = p.get_output_frame_rate();
        Exporter {
            base: binding::plugin_processor { ops: &OPS },
            processor: Box::new(p),
            frame_rate,
        }
    }

    pub fn export(self) -> *mut binding::plugin_processor {
        unsafe { std::mem::transmute(Box::into_raw(Box::new(self))) }
    }
}

const OPS: binding::plugin_processor_ops = binding::plugin_processor_ops {
    run: Some(wrapper_run),
    destroy: Some(wrapper_destroy),
    get_output_frame_rate: Some(wrapper_get_output_frame_rate),
};

unsafe extern "C" fn wrapper_run(
    p: *mut binding::plugin_processor,
    input: *const binding::multi_slice,
    output: *mut binding::multi_slice,
) -> binding::status {
    let wrapper: &mut Exporter = match std::mem::transmute::<_, *mut Exporter>(p).as_mut() {
        Some(wrapper) => wrapper,
        None => return binding::status::ErrInvalidProcessor,
    };

    let input: MultiSlice<f32> = match input.as_ref() {
        Some(raw) => MultiSlice::from_raw(
            raw.data[..raw.channels]
                .iter()
                .map(|&ptr| slice::from_raw_parts_mut(ptr, raw.num_frames))
                .collect(),
        ),
        None => return binding::status::ErrInvalidArgument,
    };
    let output = match output.as_mut() {
        Some(raw) => raw,
        None => return binding::status::ErrInvalidArgument,
    };

    match wrapper.processor.process(input) {
        Ok(mut buf) => {
            output.channels = buf.channels();
            output.num_frames = buf.min_len();
            let ptrs: Vec<*mut f32> = buf.iter_mut().map(|slice| slice.as_mut_ptr()).collect();
            output.data[..buf.channels()].clone_from_slice(&ptrs);

            binding::status::StatusOk
        }
        Err(error) => {
            eprintln!("wrapper_run: {}", error);
            binding::status::ErrOther
        }
    }
}

unsafe extern "C" fn wrapper_destroy(p: *mut binding::plugin_processor) -> binding::status {
    if !p.is_null() {
        drop(Box::from_raw(&mut *p.cast::<Exporter>()))
    }

    binding::status::StatusOk
}

unsafe extern "C" fn wrapper_get_output_frame_rate(
    p: *mut binding::plugin_processor,
    output_frame_rate: *mut usize,
) -> binding::status {
    let wrapper: &mut Exporter = match std::mem::transmute::<_, *mut Exporter>(p).as_mut() {
        Some(wrapper) => wrapper,
        None => return binding::status::ErrInvalidProcessor,
    };

    *output_frame_rate = wrapper.frame_rate;

    binding::status::StatusOk
}

#[cfg(test)]
mod tests {
    use crate::processors::export_plugin;
    use crate::processors::NegateAudioProcessor;
    use crate::processors::PluginProcessor;
    use crate::processors::SpeexResampler;
    use crate::AudioProcessor;
    use crate::MultiBuffer;
    use crate::Shape;

    #[test]
    fn negate_process() {
        let mut input: MultiBuffer<f32> =
            MultiBuffer::from(vec![vec![1., 2., 3., 4.], vec![5., 6., 7., 8.]]);
        let original_ap = NegateAudioProcessor::new(2, 4, 48000);
        let ap_binding = export_plugin(original_ap);
        let mut ap = unsafe { PluginProcessor::from_handle(ap_binding) }.unwrap();

        let output = ap.process(input.as_multi_slice()).unwrap();

        // output = -input
        assert_eq!(
            output.into_raw(),
            [[-1., -2., -3., -4.], [-5., -6., -7., -8.]]
        );

        // non-in-place: input does not change
        assert_eq!(input.to_vecs(), [[1., 2., 3., 4.], [5., 6., 7., 8.]]);
    }

    #[test]
    fn get_output_frame_rate() {
        let original_ap = SpeexResampler::new(
            Shape {
                channels: 1,
                frames: 5,
            },
            16000,
            48000,
        )
        .unwrap();
        let ap_binding = export_plugin(original_ap);
        let ap = unsafe { PluginProcessor::from_handle(ap_binding) }.unwrap();

        assert_eq!(ap.get_output_frame_rate(), 48000);
    }
}
