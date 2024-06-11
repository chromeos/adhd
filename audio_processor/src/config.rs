// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use std::fmt::Debug;
use std::path::PathBuf;

use anyhow::bail;
use anyhow::Context;
use hound::WavSpec;
use hound::WavWriter;
use serde::Deserialize;
use serde::Serialize;

use crate::processors::ChunkWrapper;
use crate::processors::DynamicPluginProcessor;
use crate::processors::NegateAudioProcessor;
use crate::processors::SpeexResampler;
use crate::AudioProcessor;
use crate::Format;
use crate::Pipeline;
use crate::ProcessorVec;

#[derive(Debug, Serialize, Deserialize)]
pub enum Processor {
    Negate,
    WavSink {
        path: PathBuf,
    },
    Plugin {
        path: PathBuf,
        constructor: String,
    },
    WrapChunk {
        inner: Box<Processor>,
        inner_block_size: usize,
    },
    Resample {
        output_frame_rate: usize,
    },
    Pipeline {
        processors: Vec<Processor>,
    },
    Preloaded(PreloadedProcessor),
    ShuffleChannels {
        channel_indexes: Vec<usize>,
    },
    /// Checks the current format of the pipeline.
    /// Does not actually builds a processor,
    /// so unlike `crate::processors::CheckShape`, does not perform checks
    /// when processing audio.
    CheckFormat {
        channels: Option<usize>,
        block_size: Option<usize>,
        frame_rate: Option<usize>,
    },
}

/// PreloadedProcessor is a config that describes a processor that is already created
/// out of the config system.
pub struct PreloadedProcessor {
    description: &'static str,
    processor: Box<dyn AudioProcessor<I = f32, O = f32> + Send>,
}

impl Serialize for PreloadedProcessor {
    fn serialize<S>(&self, _serializer: S) -> Result<S::Ok, S::Error>
    where
        S: serde::Serializer,
    {
        Err(serde::ser::Error::custom(
            "PreloadedProcessor cannot be serialized",
        ))
    }
}

impl<'de> Deserialize<'de> for PreloadedProcessor {
    fn deserialize<D>(_deserializer: D) -> Result<Self, D::Error>
    where
        D: serde::Deserializer<'de>,
    {
        Err(serde::de::Error::custom(
            "PreloadedProcessor cannot be deserialized",
        ))
    }
}

impl Debug for PreloadedProcessor {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        f.debug_struct("Preloaded")
            .field("description", &self.description)
            .finish()
    }
}

struct Config {
    input_format: Format,
    pipeline: ProcessorVec,
}

impl Config {
    fn new(input_format: Format) -> Self {
        Self {
            input_format,
            pipeline: vec![],
        }
    }

    fn output_format(&self) -> Format {
        self.pipeline
            .get_last_output_format()
            .unwrap_or(self.input_format)
    }

    fn add(&mut self, config: Processor) -> anyhow::Result<()> {
        use Processor::*;
        match config {
            Negate => {
                self.pipeline
                    .add(NegateAudioProcessor::new(self.output_format()));
            }
            WavSink { path } => {
                let output_format = self.output_format();
                self.pipeline.add(crate::processors::WavSink::new(
                    WavWriter::create(
                        &path,
                        WavSpec {
                            channels: output_format.channels.try_into().context("channels")?,
                            sample_format: hound::SampleFormat::Float,
                            sample_rate: output_format
                                .frame_rate
                                .try_into()
                                .context("sample_rate")?,
                            bits_per_sample: 32,
                        },
                    )
                    .context("WavWriter::create")?,
                    output_format.block_size,
                ));
            }
            Plugin { path, constructor } => {
                self.pipeline.add(
                    DynamicPluginProcessor::new(
                        path.to_str().context("path.to_str")?,
                        &constructor,
                        self.output_format(),
                    )
                    .context("DynamicPluginProcessor::new")?,
                );
            }
            WrapChunk {
                inner,
                inner_block_size,
            } => {
                if self.output_format().block_size == inner_block_size {
                    self.add(*inner).context("inner")?;
                } else {
                    let inner_pipeline = build_pipeline(
                        Format {
                            block_size: inner_block_size,
                            ..self.output_format()
                        },
                        *inner,
                    )
                    .context("inner")?;

                    let inner_channels = inner_pipeline.get_output_format().channels;
                    // TODO: When the inner_pipeline only has a single processor, wrap just that processor.
                    self.pipeline.add(ChunkWrapper::new(
                        inner_pipeline,
                        self.output_format().block_size,
                        inner_block_size,
                        self.output_format().channels,
                        inner_channels,
                    ));
                }
            }
            Resample { output_frame_rate } => {
                if self.output_format().frame_rate != output_frame_rate {
                    self.pipeline.add(
                        SpeexResampler::new(self.output_format(), output_frame_rate)
                            .context("SpeexResampler::new")?,
                    );
                }
            }
            Pipeline { processors } => {
                for (i, config) in processors.into_iter().enumerate() {
                    self.add(config)
                        .with_context(|| format!("pipeline processor#{i}"))?;
                }
            }
            Preloaded(PreloadedProcessor { processor, .. }) => {
                self.pipeline.push(processor);
            }
            ShuffleChannels { channel_indexes } => self.pipeline.add(
                crate::processors::ShuffleChannels::new(&channel_indexes, self.output_format()),
            ),
            CheckFormat {
                channels,
                block_size,
                frame_rate,
            } => {
                let format = self.output_format();
                if let Some(channels) = channels {
                    if channels != format.channels {
                        bail!("expected channels {channels:?} got {format:?}");
                    }
                }
                if let Some(block_size) = block_size {
                    if block_size != format.block_size {
                        bail!("expected block_size {block_size:?} got {format:?}");
                    }
                }
                if let Some(frame_rate) = frame_rate {
                    if frame_rate != format.frame_rate {
                        bail!("expected frame_rate {frame_rate:?} got {format:?}");
                    }
                }
            }
        }
        Ok(())
    }
}

/// Build a pipeline from the given configuration.
pub fn build_pipeline(input_format: Format, config: Processor) -> anyhow::Result<ProcessorVec> {
    let mut builder = Config::new(input_format);
    builder.add(config)?;
    Ok(builder.pipeline)
}

#[cfg(test)]
mod tests {
    use std::env;

    use assert_matches::assert_matches;
    use hound::WavSpec;

    use crate::config::build_pipeline;
    use crate::config::PreloadedProcessor;
    use crate::config::Processor;
    use crate::processors::NegateAudioProcessor;
    use crate::util::read_wav;
    use crate::AudioProcessor;
    use crate::Format;
    use crate::MultiBuffer;

    #[test]
    fn simple_pipeline() {
        use Processor::*;

        let tempdir = tempfile::tempdir().unwrap();

        let config = Pipeline {
            processors: vec![
                WavSink {
                    path: tempdir.path().join("0.wav"),
                },
                Negate,
                WavSink {
                    path: tempdir.path().join("1.wav"),
                },
                WrapChunk {
                    inner: Box::new(Negate),
                    inner_block_size: 1,
                },
                WavSink {
                    path: tempdir.path().join("2.wav"),
                },
                WrapChunk {
                    inner: Box::new(Pipeline {
                        processors: vec![
                            WavSink {
                                path: tempdir.path().join("3.wav"),
                            },
                            Negate,
                            WavSink {
                                path: tempdir.path().join("4.wav"),
                            },
                        ],
                    }),
                    inner_block_size: 2,
                },
                WrapChunk {
                    inner_block_size: 5, // Same block size, should pass through.
                    inner: Box::new(WavSink {
                        path: tempdir.path().join("5.wav"),
                    }),
                },
                Resample {
                    output_frame_rate: 48000,
                },
                WavSink {
                    path: tempdir.path().join("6.wav"),
                },
            ],
        };
        let mut pipeline = build_pipeline(
            Format {
                channels: 2,
                block_size: 5,
                frame_rate: 24000,
            },
            config,
        )
        .unwrap();

        let mut input =
            MultiBuffer::from(vec![vec![1f32, 2., 3., 4., 5.], vec![6., 7., 8., 9., 10.]]);
        let output = MultiBuffer::from(pipeline.process(input.as_multi_slice()).unwrap());

        // Drop pipeline to flush WavSinks.
        drop(pipeline);

        let mut wavs = vec![];
        for i in 0..7 {
            let path = tempdir.path().join(format!("{i}.wav"));
            wavs.push(read_wav::<f32>(&path).unwrap());
        }

        assert_matches!(
            wavs[0].0,
            WavSpec {
                channels: 2,
                sample_rate: 24000,
                ..
            }
        );
        assert_eq!(
            wavs[0].1.to_vecs(),
            [[1.0, 2.0, 3.0, 4.0, 5.0], [6.0, 7.0, 8.0, 9.0, 10.0]]
        );

        assert_matches!(
            wavs[1].0,
            WavSpec {
                channels: 2,
                sample_rate: 24000,
                ..
            }
        );
        assert_eq!(
            wavs[1].1.to_vecs(),
            [
                [-1.0, -2.0, -3.0, -4.0, -5.0],
                [-6.0, -7.0, -8.0, -9.0, -10.0]
            ]
        );

        assert_matches!(
            wavs[2].0,
            WavSpec {
                channels: 2,
                sample_rate: 24000,
                ..
            }
        );
        assert_eq!(
            wavs[2].1.to_vecs(),
            [[0.0, 1.0, 2.0, 3.0, 4.0], [0.0, 6.0, 7.0, 8.0, 9.0]]
        );

        assert_matches!(
            wavs[3].0,
            WavSpec {
                channels: 2,
                sample_rate: 24000,
                ..
            }
        );
        assert_eq!(
            wavs[3].1.to_vecs(),
            [[0.0, 1.0, 2.0, 3.0], [0.0, 6.0, 7.0, 8.0]]
        );

        assert_matches!(
            wavs[4].0,
            WavSpec {
                channels: 2,
                sample_rate: 24000,
                ..
            }
        );
        assert_eq!(
            wavs[4].1.to_vecs(),
            [[-0.0, -1.0, -2.0, -3.0], [-0.0, -6.0, -7.0, -8.0]]
        );

        assert_matches!(
            wavs[5].0,
            WavSpec {
                channels: 2,
                sample_rate: 24000,
                ..
            }
        );
        assert_eq!(
            wavs[5].1.to_vecs(),
            [[0.0, 0.0, -0.0, -1.0, -2.0], [0.0, 0.0, -0.0, -6.0, -7.0]]
        );

        assert_matches!(
            wavs[6].0,
            WavSpec {
                channels: 2,
                sample_rate: 48000,
                ..
            }
        );
        assert_eq!(wavs[6].1.to_vecs(), output.to_vecs());
    }

    #[test]
    #[cfg(feature = "bazel")]
    fn abs_process() {
        let mut input: MultiBuffer<f32> =
            MultiBuffer::from(vec![vec![1., -2., 3., -4.], vec![5., -6., 7., -8.]]);

        let mut pipeline = build_pipeline(
            Format {
                channels: 2,
                block_size: 4,
                frame_rate: 48000,
            },
            Processor::Plugin {
                path: env::var("LIBTEST_PLUGINS_SO").unwrap().into(),
                constructor: "abs_processor_create".into(),
            },
        )
        .unwrap();

        let output = pipeline.process(input.as_multi_slice()).unwrap();

        // output = abs(input)
        assert_eq!(output.into_raw(), [[1., 2., 3., 4.], [5., 6., 7., 8.]]);
    }

    #[test]
    fn preloaded() {
        let mut input: MultiBuffer<f32> =
            MultiBuffer::from(vec![vec![1., 2., 3., 4.], vec![5., 6., 7., 8.]]);

        let input_format = Format {
            channels: 2,
            block_size: 4,
            frame_rate: 48000,
        };

        let mut pipeline = build_pipeline(
            input_format,
            Processor::Preloaded(PreloadedProcessor {
                description: "preloaded negate",
                processor: Box::new(NegateAudioProcessor::new(input_format)),
            }),
        )
        .unwrap();

        let output = pipeline.process(input.as_multi_slice()).unwrap();

        // output = abs(input)
        assert_eq!(
            output.into_raw(),
            [[-1., -2., -3., -4.], [-5., -6., -7., -8.]]
        );
    }

    #[test]
    fn shuffle_channels() {
        let mut pipeline = build_pipeline(
            Format {
                channels: 2,
                block_size: 2,
                frame_rate: 48000,
            },
            Processor::ShuffleChannels {
                channel_indexes: vec![1, 0, 1],
            },
        )
        .unwrap();

        let mut input = MultiBuffer::from(vec![vec![1., 2.], vec![3., 4.]]);
        let output = pipeline.process(input.as_multi_slice()).unwrap();
        assert_eq!(output.into_raw(), [[3., 4.], [1., 2.], [3., 4.]]);
    }

    #[test]
    fn check_format() {
        let Err(err) = build_pipeline(
            Format {
                channels: 2,
                block_size: 2,
                frame_rate: 48000,
            },
            Processor::CheckFormat {
                channels: Some(3),
                block_size: None,
                frame_rate: None,
            },
        ) else {
            panic!("should fail");
        };
        assert!(err.to_string().contains("expected channels 3"), "{err}");

        let Err(err) = build_pipeline(
            Format {
                channels: 2,
                block_size: 2,
                frame_rate: 48000,
            },
            Processor::CheckFormat {
                channels: None,
                block_size: Some(1),
                frame_rate: None,
            },
        ) else {
            panic!("should fail");
        };
        assert!(err.to_string().contains("expected block_size 1"), "{err}");

        let Err(err) = build_pipeline(
            Format {
                channels: 2,
                block_size: 2,
                frame_rate: 48000,
            },
            Processor::CheckFormat {
                channels: None,
                block_size: None,
                frame_rate: Some(99999),
            },
        ) else {
            panic!("should fail");
        };
        assert!(
            err.to_string().contains("expected frame_rate 99999"),
            "{err}"
        );
    }
}
