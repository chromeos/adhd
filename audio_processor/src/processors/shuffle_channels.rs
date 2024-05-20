// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::AudioProcessor;
use crate::Format;
use crate::MultiBuffer;
use crate::MultiSlice;
use crate::Shape;

/// ChannelMap is a processor that shuffles channels.
pub struct ShuffleChannels {
    channel_indexes: Vec<usize>,
    buffer: MultiBuffer<f32>,
    output_format: Format,
}

impl ShuffleChannels {
    /// Create a new `ChannelMap` processor which shuffles channels.
    /// output[i] will be assigned with input[channel_indexes[i]].
    pub fn new(channel_indexes: &[usize], input_format: Format) -> Self {
        for &channel in channel_indexes {
            assert!(
                channel < input_format.channels,
                "channel out of bounds! channel_indexes={channel_indexes:?}, input_format={input_format:?}",
            );
        }
        Self {
            channel_indexes: Vec::from(channel_indexes),
            buffer: MultiBuffer::new(Shape {
                channels: channel_indexes.len(),
                frames: input_format.block_size,
            }),
            output_format: Format {
                channels: channel_indexes.len(),
                ..input_format
            },
        }
    }
}

impl AudioProcessor for ShuffleChannels {
    type I = f32;
    type O = f32;

    fn process<'a>(
        &'a mut self,
        input: MultiSlice<'a, Self::I>,
    ) -> crate::Result<MultiSlice<'a, Self::O>> {
        for (data, &index) in self
            .buffer
            .as_multi_slice()
            .iter_mut()
            .zip(self.channel_indexes.iter())
        {
            data.clone_from_slice(&input[index]);
        }
        Ok(self.buffer.as_multi_slice())
    }

    fn get_output_format(&self) -> Format {
        self.output_format
    }
}

#[cfg(test)]
mod tests {
    use super::ShuffleChannels;
    use crate::AudioProcessor;
    use crate::Format;
    use crate::MultiBuffer;

    #[test]
    fn map() {
        let mut p = ShuffleChannels::new(
            &[1, 0, 1],
            Format {
                channels: 2,
                block_size: 2,
                frame_rate: 48000,
            },
        );

        let mut input = MultiBuffer::from(vec![vec![1., 2.], vec![3., 4.]]);
        let output = p.process(input.as_multi_slice()).unwrap();
        assert_eq!(output.into_raw(), [[3., 4.], [1., 2.], [3., 4.]]);
    }
}
