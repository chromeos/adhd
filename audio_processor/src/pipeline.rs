// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use std::path::Path;

use anyhow::Context;
use hound::WavSpec;
use hound::WavWriter;

use crate::processors::WavSink;
use crate::AudioProcessor;
use crate::Format;
use crate::MultiSlice;

pub type ProcessorVec = Vec<Box<dyn AudioProcessor<I = f32, O = f32> + Send>>;

pub struct Pipeline {
    pub input_format: Format,
    pub vec: ProcessorVec,
}

impl Pipeline {
    pub fn new(input_format: Format) -> Self {
        Pipeline {
            input_format,
            vec: Vec::new(),
        }
    }

    pub fn add(&mut self, processor: impl AudioProcessor<I = f32, O = f32> + Send + 'static) {
        self.vec.push(Box::new(processor));
    }

    pub fn add_wav_dump(&mut self, filename: &Path) -> anyhow::Result<()> {
        let format = self.get_output_format();
        {
            self.add(WavSink::new(
                WavWriter::create(
                    filename,
                    WavSpec {
                        channels: format.channels.try_into().context("channels.try_into()")?,
                        sample_rate: format
                            .frame_rate
                            .try_into()
                            .context("frame_rate.try_into()")?,
                        bits_per_sample: 32,
                        sample_format: hound::SampleFormat::Float,
                    },
                )
                .context("WavWriter::create")?,
                format.block_size,
            ));
            anyhow::Result::<()>::Ok(())
        }
        .context("add_wav_dump")?;
        Ok(())
    }
}

impl AudioProcessor for Pipeline {
    type I = f32;
    type O = f32;

    fn process<'a>(
        &'a mut self,
        mut input: MultiSlice<'a, Self::I>,
    ) -> crate::Result<MultiSlice<'a, Self::O>> {
        for processor in self.vec.iter_mut() {
            input = processor.process(input)?;
        }
        Ok(input)
    }

    fn get_output_format(&self) -> Format {
        match self.vec.last() {
            Some(last) => last.get_output_format(),
            None => self.input_format,
        }
    }
}

#[cfg(test)]
mod tests {
    use anyhow::Context;
    use tempfile::TempDir;

    use crate::processors::NegateAudioProcessor;
    use crate::util::read_wav;
    use crate::AudioProcessor;
    use crate::Format;
    use crate::MultiBuffer;
    use crate::Pipeline;

    #[test]
    fn test_pipeline() -> anyhow::Result<()> {
        let tmpd = TempDir::new().context("tempdir")?;

        let dump1 = tmpd.path().join("dump-1.wav");
        let dump2 = tmpd.path().join("dump-2.wav");

        let mut p = Pipeline::new(Format {
            channels: 1,
            block_size: 4,
            frame_rate: 48000,
        });

        p.add_wav_dump(&dump1).unwrap();
        p.add(NegateAudioProcessor::new(Format {
            channels: 1,
            block_size: 4,
            frame_rate: 48000,
        }));
        p.add_wav_dump(&dump2).unwrap();

        assert_eq!(
            p.get_output_format(),
            Format {
                channels: 1,
                block_size: 4,
                frame_rate: 48000,
            }
        );

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
