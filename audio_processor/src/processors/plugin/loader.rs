// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use std::path::PathBuf;

use anyhow::anyhow;
use anyhow::Context;

use crate::processors::ChunkWrapper;
use crate::processors::DynamicPluginProcessor;
use crate::processors::SpeexResampler;
use crate::AudioProcessor;
use crate::Format;
use crate::Pipeline;
use crate::ProcessorVec;

/// Configuration for plugin dump.
pub struct PluginDumpConfig {
    /// Path to store the pre-processing WAVE dump.
    pub pre_processing_wav_dump: PathBuf,
    /// Path to store the post-processing WAVE dump.
    pub post_processing_wav_dump: PathBuf,
}

// TODO(aaronyu): This is the wrong abstraction. We should have an API to
// chain processors with incompatible shapes and rates easily instead,
// when we have something that looks more like a graph.
pub struct PluginLoader<'a> {
    pub path: &'a str,
    pub constructor: &'a str,
    pub channels: usize,
    pub outer_rate: usize,
    pub inner_rate: usize,
    pub outer_block_size: usize,
    pub inner_block_size: usize,
    pub allow_chunk_wrapper: bool,
    pub dump_config: Option<&'a PluginDumpConfig>,
}

impl<'a> PluginLoader<'a> {
    pub fn load_and_wrap(self) -> anyhow::Result<ProcessorVec> {
        let processor = DynamicPluginProcessor::new(
            self.path,
            self.constructor,
            Format {
                channels: self.channels,
                block_size: self.inner_block_size,
                frame_rate: self.inner_rate,
            },
        )
        .with_context(|| "DynamicPluginProcessor::new failed")?;

        let mut maybe_dump_processor: ProcessorVec = vec![];
        match self.dump_config {
            Some(dump_config) => {
                maybe_dump_processor.add_wav_dump(
                    &dump_config.pre_processing_wav_dump,
                    self.channels,
                    self.inner_rate,
                )?;
                maybe_dump_processor.add(processor);
                maybe_dump_processor.add_wav_dump(
                    &dump_config.post_processing_wav_dump,
                    self.channels,
                    self.inner_rate,
                )?;
            }
            None => {
                maybe_dump_processor.add(processor);
            }
        }

        let maybe_wrapped_processor: Box<dyn AudioProcessor<I = f32, O = f32> + Send> = if self
            .outer_block_size
            * self.inner_rate
            != self.inner_block_size * self.outer_rate
        {
            // The block size after resampling needs wrapping.
            if !self.allow_chunk_wrapper {
                return Err(anyhow!("ChunkWrapper is not allowed but required: outer rate={}, block_size={}; inner rate={}, block_size={}", self.outer_rate, self.outer_block_size, self.inner_rate, self.inner_block_size));
            }
            Box::new(ChunkWrapper::new(
                maybe_dump_processor,
                self.outer_block_size,
                self.inner_block_size,
                self.channels,
                self.channels,
            ))
        } else {
            Box::new(maybe_dump_processor)
        };

        let processors = if self.outer_rate == self.inner_rate {
            vec![maybe_wrapped_processor]
        } else {
            vec![
                Box::new(
                    SpeexResampler::new(
                        Format {
                            frame_rate: self.outer_rate,
                            channels: self.channels,
                            block_size: self.outer_block_size,
                        },
                        self.inner_rate,
                    )
                    .with_context(|| "failed to create 1st wrapping resampler")?,
                ),
                maybe_wrapped_processor,
                Box::new(
                    SpeexResampler::new(
                        Format {
                            frame_rate: self.inner_rate,
                            channels: self.channels,
                            block_size: self.outer_block_size * self.inner_rate / self.outer_rate,
                        },
                        self.outer_rate,
                    )
                    .with_context(|| "failed to create 2nd wrapping resampler")?,
                ),
            ]
        };

        Ok(processors)
    }
}

#[cfg(feature = "bazel")]
#[cfg(test)]
mod tests {
    use std::env;

    use tempfile::TempDir;

    use super::PluginDumpConfig;
    use super::PluginLoader;
    use crate::util::read_wav;
    use crate::AudioProcessor;
    use crate::MultiBuffer;

    #[test]
    fn test_wav_dump() {
        let plugin_path = env::var("LIBTEST_PLUGINS_SO").unwrap();

        let tmpd = TempDir::new().unwrap();

        let dumps = PluginDumpConfig {
            pre_processing_wav_dump: tmpd.path().join("pre.wav"),
            post_processing_wav_dump: tmpd.path().join("post.wav"),
        };
        let mut pipeline = PluginLoader {
            path: &plugin_path,
            constructor: "negate_processor_create",
            channels: 2,
            outer_rate: 16000,
            inner_rate: 16000,
            outer_block_size: 3,
            inner_block_size: 4,
            allow_chunk_wrapper: true,
            dump_config: Some(&dumps),
        }
        .load_and_wrap()
        .unwrap();

        let mut input = MultiBuffer::from(vec![vec![1f32, 2., 3.], vec![4., 5., 6.]]);
        let output = pipeline.process(input.as_multi_slice()).unwrap();
        assert_eq!(output.into_raw(), [[0.; 3]; 2]);

        let mut input = MultiBuffer::from(vec![vec![7., 8., 9.], vec![10., 11., 12.]]);
        let output = pipeline.process(input.as_multi_slice()).unwrap();
        assert_eq!(output.into_raw(), [[0., -1., -2.], [0., -4., -5.]]);

        // Flush wav buffers.
        drop(pipeline);

        let (pre_spec, pre_wav) = read_wav::<f32>(&dumps.pre_processing_wav_dump).unwrap();
        let (post_spec, post_wav) = read_wav::<f32>(&dumps.post_processing_wav_dump).unwrap();
        assert_eq!(pre_spec.sample_rate, 16000);
        assert_eq!(post_spec.sample_rate, 16000);
        assert_eq!(pre_wav.to_vecs(), [[1., 2., 3., 7.], [4., 5., 6., 10.]]);
        assert_eq!(
            post_wav.to_vecs(),
            [[-1.0, -2.0, -3.0, -7.0], [-4.0, -5.0, -6.0, -10.0]]
        );
    }
}
