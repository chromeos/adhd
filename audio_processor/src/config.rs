// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use std::fmt::Debug;
use std::path::PathBuf;
use std::rc::Rc;
use std::sync::mpsc::Sender;

use anyhow::bail;
use anyhow::Context;
use hound::WavSpec;
use hound::WavWriter;
use serde::Deserialize;
use serde::Serialize;

use crate::processors::peer::ManagedBlockingSeqPacketProcessor;
use crate::processors::peer::ThreadedWorkerFactory;
use crate::processors::peer::WorkerFactory;
use crate::processors::profile::Profile;
use crate::processors::profile::ProfileStats;
use crate::processors::ChunkWrapper;
use crate::processors::DynamicPluginProcessor;
use crate::processors::InPlaceNegateAudioProcessor;
use crate::processors::SpeexResampler;
use crate::AudioProcessor;
use crate::Format;
use crate::Pipeline;

#[derive(Debug, Serialize, Deserialize, PartialEq, Eq)]
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
        /// Prevents merging with the outer pipeline even if they have the same block size.
        /// Used when the outer pipeline doesn't actually have a stable block size and the
        /// ChunkWrapper is used for regulating it.
        disallow_hoisting: bool,
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
    Nothing,
    Peer {
        processor: Box<Processor>,
    },
}

/// PreloadedProcessor is a config that describes a processor that is already created
/// out of the config system.
pub struct PreloadedProcessor {
    pub description: &'static str,
    pub processor: Box<dyn AudioProcessor<I = f32, O = f32> + Send>,
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

impl PartialEq for PreloadedProcessor {
    fn eq(&self, other: &Self) -> bool {
        std::ptr::eq(self, other)
    }
}

impl Eq for PreloadedProcessor {}

pub struct PipelineBuilder {
    pipeline: Pipeline,
    profile_sender: Option<Sender<ProfileStats>>,
    worker_factory: Rc<dyn WorkerFactory>,
}

impl PipelineBuilder {
    pub fn new(input_format: Format) -> Self {
        Self {
            pipeline: Pipeline::new(input_format),
            profile_sender: None,
            worker_factory: Rc::new(ThreadedWorkerFactory),
        }
    }

    pub fn build(mut self, config: Processor) -> anyhow::Result<Pipeline> {
        self.add(config)?;
        Ok(self.pipeline)
    }

    /// Enable profiling. The results are sent with sender when the pipeline
    /// is dropped.
    /// Currently only `Plugin`s are profiled.
    pub fn with_profile_sender(mut self, sender: Sender<ProfileStats>) -> Self {
        self.profile_sender = Some(sender);
        self
    }

    /// Use the factory to spawn peer workers.
    pub fn with_worker_factory(mut self, factory: impl WorkerFactory + 'static) -> Self {
        self.worker_factory = Rc::new(factory);
        self
    }

    fn output_format(&self) -> Format {
        self.pipeline.get_output_format()
    }

    fn child_builder(&self, input_format: Format) -> Self {
        Self {
            pipeline: Pipeline::new(input_format),
            profile_sender: self.profile_sender.clone(),
            worker_factory: self.worker_factory.clone(),
        }
    }

    fn add(&mut self, config: Processor) -> anyhow::Result<()> {
        use Processor::*;
        match config {
            Negate => {
                self.pipeline
                    .add(InPlaceNegateAudioProcessor::new(self.output_format()));
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
                let plugin = DynamicPluginProcessor::new(
                    path.to_str().context("path.to_str")?,
                    &constructor,
                    self.output_format(),
                )
                .context("DynamicPluginProcessor::new")?;
                if let Some(sender) = &self.profile_sender {
                    let mut profile = Profile::new(plugin);
                    profile.set_key(format!(
                        "{}@{}",
                        constructor,
                        path.file_name()
                            .context("path.file_name() failed")?
                            .to_string_lossy(),
                    ));
                    profile.set_sender(sender.clone());
                    self.pipeline.add(profile);
                } else {
                    self.pipeline.add(plugin);
                }
            }
            WrapChunk {
                inner,
                inner_block_size,
                disallow_hoisting,
            } => {
                if self.output_format().block_size == inner_block_size && !disallow_hoisting {
                    self.add(*inner).context("inner")?;
                } else {
                    let inner_pipeline = self
                        .child_builder(Format {
                            block_size: inner_block_size,
                            ..self.output_format()
                        })
                        .build(*inner)
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
                self.pipeline.vec.push(processor);
            }
            ShuffleChannels { channel_indexes } => {
                // Optimization: only shuffle channels if it would change the
                // channel layout.
                if !channel_indexes
                    .iter()
                    .cloned()
                    .eq(0..self.output_format().channels)
                {
                    self.pipeline.add(crate::processors::ShuffleChannels::new(
                        &channel_indexes,
                        self.output_format(),
                    ))
                }
            }
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
            Nothing => {
                // Do nothing.
            }
            Peer { processor } => {
                self.pipeline.add(
                    ManagedBlockingSeqPacketProcessor::new(
                        self.worker_factory.as_ref(),
                        self.output_format(),
                        *processor,
                    )
                    .context("ManagedBlockingSeqPacketProcessor::new")?,
                );
            }
        }
        Ok(())
    }
}

#[cfg(test)]
mod tests {
    use core::panic;

    use assert_matches::assert_matches;
    use hound::WavSpec;

    use crate::config::PipelineBuilder;
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
                    disallow_hoisting: false,
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
                    disallow_hoisting: false,
                },
                WrapChunk {
                    inner_block_size: 5, // Same block size, should pass through.
                    inner: Box::new(WavSink {
                        path: tempdir.path().join("5.wav"),
                    }),
                    disallow_hoisting: false,
                },
                Resample {
                    output_frame_rate: 48000,
                },
                WavSink {
                    path: tempdir.path().join("6.wav"),
                },
            ],
        };
        let mut pipeline = PipelineBuilder::new(Format {
            channels: 2,
            block_size: 5,
            frame_rate: 24000,
        })
        .build(config)
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
    fn preloaded() {
        let mut input: MultiBuffer<f32> =
            MultiBuffer::from(vec![vec![1., 2., 3., 4.], vec![5., 6., 7., 8.]]);

        let input_format = Format {
            channels: 2,
            block_size: 4,
            frame_rate: 48000,
        };

        let mut pipeline = PipelineBuilder::new(input_format)
            .build(Processor::Preloaded(PreloadedProcessor {
                description: "preloaded negate",
                processor: Box::new(NegateAudioProcessor::new(input_format)),
            }))
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
        let mut pipeline = PipelineBuilder::new(Format {
            channels: 2,
            block_size: 2,
            frame_rate: 48000,
        })
        .build(Processor::ShuffleChannels {
            channel_indexes: vec![1, 0, 1],
        })
        .unwrap();

        let mut input = MultiBuffer::from(vec![vec![1., 2.], vec![3., 4.]]);
        let output = pipeline.process(input.as_multi_slice()).unwrap();
        assert_eq!(output.into_raw(), [[3., 4.], [1., 2.], [3., 4.]]);
    }

    #[test]
    fn shuffle_channels_opt() {
        let pipeline = PipelineBuilder::new(Format {
            channels: 2,
            block_size: 2,
            frame_rate: 48000,
        })
        .build(Processor::ShuffleChannels {
            channel_indexes: vec![1, 0],
        })
        .unwrap();
        assert_eq!(
            pipeline.vec.len(),
            1,
            "channel swap, should not be optimized away"
        );

        let pipeline = PipelineBuilder::new(Format {
            channels: 2,
            block_size: 2,
            frame_rate: 48000,
        })
        .build(Processor::ShuffleChannels {
            channel_indexes: vec![0, 1, 1],
        })
        .unwrap();
        assert_eq!(
            pipeline.vec.len(),
            1,
            "different length, should not be optimized away"
        );

        let pipeline = PipelineBuilder::new(Format {
            channels: 2,
            block_size: 2,
            frame_rate: 48000,
        })
        .build(Processor::ShuffleChannels {
            channel_indexes: vec![0],
        })
        .unwrap();
        assert_eq!(
            pipeline.vec.len(),
            1,
            "different length, should not be optimized away"
        );

        let pipeline = PipelineBuilder::new(Format {
            channels: 2,
            block_size: 2,
            frame_rate: 48000,
        })
        .build(Processor::ShuffleChannels {
            channel_indexes: vec![0, 1],
        })
        .unwrap();
        assert_eq!(pipeline.vec.len(), 0, "should optimize");
    }

    #[test]
    fn check_format() {
        let Err(err) = PipelineBuilder::new(Format {
            channels: 2,
            block_size: 2,
            frame_rate: 48000,
        })
        .build(Processor::CheckFormat {
            channels: Some(3),
            block_size: None,
            frame_rate: None,
        }) else {
            panic!("should fail");
        };
        assert!(err.to_string().contains("expected channels 3"), "{err}");

        let Err(err) = PipelineBuilder::new(Format {
            channels: 2,
            block_size: 2,
            frame_rate: 48000,
        })
        .build(Processor::CheckFormat {
            channels: None,
            block_size: Some(1),
            frame_rate: None,
        }) else {
            panic!("should fail");
        };
        assert!(err.to_string().contains("expected block_size 1"), "{err}");

        let Err(err) = PipelineBuilder::new(Format {
            channels: 2,
            block_size: 2,
            frame_rate: 48000,
        })
        .build(Processor::CheckFormat {
            channels: None,
            block_size: None,
            frame_rate: Some(99999),
        }) else {
            panic!("should fail");
        };
        assert!(
            err.to_string().contains("expected frame_rate 99999"),
            "{err}"
        );
    }

    #[test]
    fn peer() {
        let mut input: MultiBuffer<f32> =
            MultiBuffer::from(vec![vec![1., 2., 3., 4.], vec![5., 6., 7., 8.]]);

        let input_format = Format {
            channels: 2,
            block_size: 4,
            frame_rate: 48000,
        };

        let mut pipeline = PipelineBuilder::new(input_format)
            .build(Processor::Peer {
                processor: Box::new(Processor::Negate),
            })
            .unwrap();

        let output = pipeline.process(input.as_multi_slice()).unwrap();

        // output = abs(input)
        assert_eq!(
            output.into_raw(),
            [[-1., -2., -3., -4.], [-5., -6., -7., -8.]]
        );
    }
}

#[cfg(test)]
#[cfg(feature = "bazel")]
mod bazel_tests {
    use std::env;
    use std::sync::mpsc::channel;
    use std::time::Duration;

    use crate::config::PipelineBuilder;
    use crate::config::Processor;
    use crate::AudioProcessor;
    use crate::Format;
    use crate::MultiBuffer;

    #[test]
    fn abs_process() {
        let mut input: MultiBuffer<f32> =
            MultiBuffer::from(vec![vec![1., -2., 3., -4.], vec![5., -6., 7., -8.]]);

        let mut pipeline = PipelineBuilder::new(Format {
            channels: 2,
            block_size: 4,
            frame_rate: 48000,
        })
        .build(Processor::Plugin {
            path: std::env::var("LIBTEST_PLUGINS_SO").unwrap().into(),
            constructor: "abs_processor_create".into(),
        })
        .unwrap();

        let output = pipeline.process(input.as_multi_slice()).unwrap();

        // output = abs(input)
        assert_eq!(output.into_raw(), [[1., 2., 3., 4.], [5., 6., 7., 8.]]);
    }

    #[test]
    fn profile() {
        let test_plugin_path: std::path::PathBuf = env::var("LIBTEST_PLUGINS_SO").unwrap().into();
        let (sender, receiver) = channel();

        let pipeline = PipelineBuilder::new(Format {
            channels: 2,
            block_size: 2,
            frame_rate: 48000,
        })
        .with_profile_sender(sender)
        .build(Processor::Pipeline {
            processors: vec![
                Processor::Plugin {
                    path: test_plugin_path.clone(),
                    constructor: "abs_processor_create".into(),
                },
                Processor::WrapChunk {
                    inner: Box::new(Processor::Pipeline {
                        processors: vec![
                            Processor::Plugin {
                                path: test_plugin_path.clone(),
                                constructor: "negate_processor_create".into(),
                            },
                            Processor::Plugin {
                                path: test_plugin_path.clone(),
                                constructor: "echo_processor_create".into(),
                            },
                        ],
                    }),
                    inner_block_size: 4096,
                    disallow_hoisting: false,
                },
            ],
        })
        .unwrap();

        assert!(receiver.recv_timeout(Duration::ZERO).is_err());

        drop(pipeline);
        let mut keys = receiver
            .into_iter()
            .map(|stat| stat.key)
            .collect::<Vec<_>>();
        keys.sort();
        let basename = test_plugin_path.file_name().unwrap().to_str().unwrap();
        assert_eq!(
            keys,
            [
                format!("abs_processor_create@{basename}"),
                format!("echo_processor_create@{basename}"),
                format!("negate_processor_create@{basename}")
            ]
        );
    }
}
