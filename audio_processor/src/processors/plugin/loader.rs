// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use anyhow::anyhow;
use anyhow::Context;

use crate::processors::ChunkWrapper;
use crate::processors::DynamicPluginProcessor;
use crate::processors::SpeexResampler;
use crate::AudioProcessor;
use crate::ProcessorVec;
use crate::Shape;

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
}

impl<'a> PluginLoader<'a> {
    pub fn load_and_wrap(self) -> anyhow::Result<ProcessorVec> {
        let processor = DynamicPluginProcessor::new(
            self.path,
            self.constructor,
            self.inner_block_size,
            self.channels,
            self.inner_rate,
        )
        .with_context(|| "DynamicPluginProcessor::new failed")?;

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
                processor,
                self.inner_block_size,
                self.channels,
                self.channels,
            ))
        } else {
            Box::new(processor)
        };

        let processors = if self.outer_rate == self.inner_rate {
            vec![maybe_wrapped_processor]
        } else {
            vec![
                Box::new(
                    SpeexResampler::new(
                        Shape {
                            channels: self.channels,
                            frames: self.outer_block_size,
                        },
                        self.outer_rate,
                        self.inner_rate,
                    )
                    .with_context(|| "failed to create 1st wrapping resampler")?,
                ),
                maybe_wrapped_processor,
                Box::new(
                    SpeexResampler::new(
                        Shape {
                            channels: self.channels,
                            frames: self.outer_block_size * self.inner_rate / self.outer_rate,
                        },
                        self.inner_rate,
                        self.outer_rate,
                    )
                    .with_context(|| "failed to create 2nd wrapping resampler")?,
                ),
            ]
        };

        Ok(processors)
    }
}
