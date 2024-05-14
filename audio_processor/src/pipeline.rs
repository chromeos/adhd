// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use std::path::Path;

use anyhow::Context;
use hound::WavSpec;
use hound::WavWriter;

use crate::processors::WavSink;
use crate::AudioProcessor;
use crate::MultiSlice;

pub type ProcessorVec = Vec<Box<dyn AudioProcessor<I = f32, O = f32> + Send>>;

pub trait Pipeline {
    fn add(&mut self, processor: impl AudioProcessor<I = f32, O = f32> + Send + 'static);

    fn add_wav_dump(
        &mut self,
        filename: &Path,
        channels: usize,
        frame_rate: usize,
    ) -> anyhow::Result<()> {
        {
            self.add(WavSink::new(
                WavWriter::create(
                    filename,
                    WavSpec {
                        channels: channels.try_into().context("channels.try_into()")?,
                        sample_rate: frame_rate.try_into().context("frame_rate.try_into()")?,
                        bits_per_sample: 32,
                        sample_format: hound::SampleFormat::Float,
                    },
                )
                .context("WavWriter::create")?,
            ));
            anyhow::Result::<()>::Ok(())
        }
        .context("add_wav_dump")?;
        Ok(())
    }
}

impl Pipeline for ProcessorVec {
    fn add(&mut self, processor: impl AudioProcessor<I = f32, O = f32> + Send + 'static) {
        self.push(Box::new(processor));
    }
}

impl AudioProcessor for ProcessorVec {
    type I = f32;
    type O = f32;

    fn process<'a>(
        &'a mut self,
        mut input: MultiSlice<'a, Self::I>,
    ) -> crate::Result<MultiSlice<'a, Self::O>> {
        for processor in self.iter_mut() {
            input = processor.process(input)?;
        }
        Ok(input)
    }

    fn get_output_frame_rate<'a>(&'a self) -> usize {
        for processor in self.iter().rev() {
            let frame_rate = processor.get_output_frame_rate();
            if frame_rate != 0 {
                // Zero means the output rate is the same as the previous processor.
                return frame_rate;
            }
        }
        0
    }
}

#[cfg(test)]
mod tests {
    use anyhow::Context;
    use tempfile::TempDir;

    use crate::processors::NegateAudioProcessor;
    use crate::util::read_wav;
    use crate::AudioProcessor;
    use crate::MultiBuffer;
    use crate::Pipeline;
    use crate::ProcessorVec;

    #[test]
    fn test_pipeline() -> anyhow::Result<()> {
        let tmpd = TempDir::new().context("tempdir")?;

        let dump1 = tmpd.path().join("dump-1.wav");
        let dump2 = tmpd.path().join("dump-2.wav");

        let mut p: ProcessorVec = vec![];

        p.add_wav_dump(&dump1, 1, 48000).unwrap();
        p.add(NegateAudioProcessor::new(1, 4, 48000));
        p.add_wav_dump(&dump2, 1, 48000).unwrap();

        assert_eq!(p.get_output_frame_rate(), 48000);

        let mut buf = MultiBuffer::from(vec![vec![1f32, 2., 3., 4.]]);
        let out = p.process(buf.as_multi_slice()).unwrap();

        // Check output.
        assert_eq!(out.into_raw(), [[-1., -2., -3., -4.]]);
        // Drop p to flush wav dumps.
        drop(p);

        let (_, wav1) = read_wav::<f32>(&dump1).unwrap();
        let (_, wav2) = read_wav::<f32>(&dump2).unwrap();
        assert_eq!(wav1.to_vecs(), [[1., 2., 3., 4.]]);
        assert_eq!(wav2.to_vecs(), [[-1., -2., -3., -4.]]);

        Ok(())
    }
}
