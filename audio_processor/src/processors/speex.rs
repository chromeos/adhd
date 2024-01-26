// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use anyhow::anyhow;
use anyhow::Context;

use self::sys::speex_resampler_destroy;
use crate::AudioProcessor;
use crate::MultiBuffer;
use crate::Shape;

mod sys {
    #![allow(non_upper_case_globals)]
    #![allow(non_camel_case_types)]
    #![allow(non_snake_case)]
    #![allow(unused)]
    include!(concat!(env!("OUT_DIR"), "/speex_sys.rs"));
}

/// Resample float PCM frames using speex_resampler.
pub struct SpeexResampler {
    resampler_state: *mut sys::SpeexResamplerState,
    output_buffer: MultiBuffer<f32>,
    output_frame_rate: usize,
}

impl SpeexResampler {
    /// Create a resampler for the given input shape, rate and output rate.
    pub fn new(input_shape: Shape, input_rate: usize, output_rate: usize) -> crate::Result<Self> {
        let output_shape = Shape {
            channels: input_shape.channels,
            frames: input_shape.frames * output_rate / input_rate,
        };
        if input_shape.frames * output_rate != output_shape.frames * input_rate {
            return Err(
                anyhow!("provided input frames {}, rate {} and output rate {} does not allow an integral output frames",
            input_shape.frames, input_rate, output_rate).into());
        }
        let mut err = std::mem::MaybeUninit::zeroed();
        // Safety: Initialization is safe.
        let resampler_state = unsafe {
            sys::speex_resampler_init(
                input_shape
                    .channels
                    .try_into()
                    .with_context(|| "channels too large")?,
                input_rate
                    .try_into()
                    .with_context(|| "input rate too large")?,
                output_rate
                    .try_into()
                    .with_context(|| "output_rate too large")?,
                4,
                err.as_mut_ptr(),
            )
        };
        // Safety: Err initialized by speex_resampler_init.
        let err = unsafe { err.assume_init() };
        if err != 0 {
            return Err(anyhow!("speex_resampler_init returned error {}", err).into());
        }
        assert_ne!(
            resampler_state,
            std::ptr::null_mut(),
            "speex_resampler_init returned non-zero err with NULL resampler"
        );

        Ok(Self {
            resampler_state,
            output_buffer: MultiBuffer::new_equilibrium(output_shape),
            output_frame_rate: output_rate,
        })
    }
}

impl Drop for SpeexResampler {
    fn drop(&mut self) {
        // Safety: `self.resampler_state` is initialized in `new`.
        unsafe { speex_resampler_destroy(self.resampler_state) }
    }
}

impl AudioProcessor for SpeexResampler {
    type I = f32;
    type O = f32;

    fn process<'a>(
        &'a mut self,
        input: crate::MultiSlice<'a, Self::I>,
    ) -> crate::Result<crate::MultiSlice<'a, Self::O>> {
        for (i, (in_ch, out_ch)) in input
            .iter()
            .zip(self.output_buffer.as_multi_slice().iter_mut())
            .enumerate()
        {
            let in_len = in_ch
                .len()
                .try_into()
                .expect("length should always fit spx_uint32_t");
            let out_len = out_ch
                .len()
                .try_into()
                .expect("length should always fit spx_uint32_t");
            let mut speex_in_len = in_len;
            let mut speex_out_len = out_len;
            // Safety: `self.resampler_state` is initialized in new.
            unsafe {
                sys::speex_resampler_process_float(
                    self.resampler_state,
                    i.try_into()
                        .expect("channels should always fit spx_uint32_t"),
                    in_ch.as_ptr(),
                    &mut speex_in_len,
                    out_ch.as_mut_ptr(),
                    &mut speex_out_len,
                );
            }
            assert_eq!(in_len, speex_in_len, "did not consume all input samples");
            assert_eq!(
                out_len, speex_out_len,
                "did not generate all output samples"
            );
        }
        Ok(self.output_buffer.as_multi_slice())
    }

    fn get_output_frame_rate<'a>(&'a self) -> usize {
        self.output_frame_rate
    }
}

#[cfg(test)]
mod tests {
    use super::SpeexResampler;
    use crate::AudioProcessor;
    use crate::MultiBuffer;
    use crate::Shape;

    /// Check that the resampler produces the same number of outputs every iteration.
    fn assert_synchronous_output(
        channels: usize,
        in_frames: usize,
        in_rate: usize,
        out_rate: usize,
    ) {
        let in_shape = Shape {
            channels,
            frames: in_frames,
        };
        let mut p = SpeexResampler::new(in_shape, in_rate, out_rate).unwrap();
        let mut input = MultiBuffer::<f32>::new_equilibrium(in_shape);
        for _ in 0..3 {
            // Run a few iterations.
            let output = p.process(input.as_multi_slice()).unwrap();
            assert_eq!(output.channels(), channels);
            assert_eq!(in_rate * output.min_len(), out_rate * in_frames);
        }
    }

    #[test]
    fn synchronous_output() {
        assert_synchronous_output(1, 441, 44100, 48000);
        assert_synchronous_output(2, 441, 44100, 48000);
        assert_synchronous_output(1, 480, 48000, 44100);
        assert_synchronous_output(1, 160, 16000, 48000);
        assert_synchronous_output(1, 480, 48000, 16000);
        assert_synchronous_output(1, 16, 16000, 48000);
        assert_synchronous_output(1, 48, 48000, 16000);
        assert_synchronous_output(1, 1, 16000, 48000);
        assert_synchronous_output(1, 3, 48000, 16000);
        assert_synchronous_output(1, 160, 16000, 24000);
        assert_synchronous_output(1, 240, 24000, 16000);
        assert_synchronous_output(1, 2, 16000, 24000);
        assert_synchronous_output(1, 3, 24000, 16000);
    }

    #[test]
    fn get_output_frame_rate() {
        let speex = SpeexResampler::new(
            Shape {
                channels: 1,
                frames: 5,
            },
            16000,
            48000,
        )
        .unwrap();

        assert_eq!(speex.get_output_frame_rate(), 48000);
    }
}
