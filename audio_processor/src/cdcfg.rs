// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use std::path::Path;
use std::path::PathBuf;

use anyhow::bail;
use anyhow::Context;

use crate::config::Processor;

pub trait ResolverContext {
    fn get_wav_dump_root(&self) -> Option<&Path>;
    fn get_dlc_root_path(&self, dlc_id: &str) -> anyhow::Result<PathBuf>;
    fn get_duplicate_channel_0(&self) -> Option<usize>;
}

#[derive(Default)]
struct NaiveResolverContext {
    wav_dump_root: Option<PathBuf>,
    duplicate_channel_0: Option<usize>,
}

impl ResolverContext for NaiveResolverContext {
    fn get_wav_dump_root(&self) -> Option<&Path> {
        self.wav_dump_root.as_deref()
    }

    fn get_dlc_root_path(&self, dlc_id: &str) -> anyhow::Result<PathBuf> {
        Ok(Path::new("/run/imageloader")
            .join(dlc_id)
            .join("package/root"))
    }

    fn get_duplicate_channel_0(&self) -> Option<usize> {
        self.duplicate_channel_0
    }
}

fn resolve(
    context: &dyn ResolverContext,
    proto: &crate::proto::cdcfg::Processor,
) -> anyhow::Result<crate::config::Processor> {
    let Some(processor) = &proto.processor_oneof else {
        bail!("processor_oneof is empty")
    };
    use crate::proto::cdcfg::processor::Processor_oneof::*;
    Ok(match processor {
        MaybeWavDump(maybe_wav_dump) => match context.get_wav_dump_root() {
            Some(wav_dump_root) => Processor::WavSink {
                path: wav_dump_root.join(maybe_wav_dump.filename.clone()),
            },
            None => Processor::Nothing,
        },
        Plugin(plugin) => Processor::Plugin {
            path: plugin.path.clone().into(),
            constructor: plugin.constructor.clone(),
        },
        DlcPlugin(dlc_plugin) => Processor::Plugin {
            path: context
                .get_dlc_root_path(&dlc_plugin.dlc_id)
                .context("context.dlc_root_path")?
                .join(&dlc_plugin.path),
            constructor: dlc_plugin.constructor.clone(),
        },
        WrapChunk(wrap_chunk) => Processor::WrapChunk {
            inner: Box::new(resolve(context, &wrap_chunk.inner).context("wrap_chunk inner")?),
            inner_block_size: wrap_chunk
                .inner_block_size
                .try_into()
                .context("wrap_chunk inner_block_size")?,
        },
        Resample(resample) => Processor::Resample {
            output_frame_rate: resample
                .output_frame_rate
                .try_into()
                .context("resample output_frame_rate")?,
        },
        Pipeline(pipeline) => Processor::Pipeline {
            processors: pipeline
                .processors
                .iter()
                .enumerate()
                .map(|(i, processor)| -> anyhow::Result<Processor> {
                    resolve(context, processor).with_context(|| format!("pipeline processor {i}"))
                })
                .collect::<Result<Vec<_>, _>>()?,
        },
        ShuffleChannels(shuffle_channels) => Processor::ShuffleChannels {
            channel_indexes: shuffle_channels
                .channel_indexes
                .iter()
                .cloned()
                .map(usize::try_from)
                .collect::<Result<Vec<_>, _>>()
                .context("shuffle_channels channel_indexes")?,
        },
        MaybeDuplicateChannel0(_) => match context.get_duplicate_channel_0() {
            Some(count) => Processor::ShuffleChannels {
                channel_indexes: vec![0; count],
            },
            None => Processor::Nothing,
        },
        CheckFormat(check_format) => Processor::CheckFormat {
            channels: check_format.channels.try_into().ok(),
            block_size: check_format.block_size.try_into().ok(),
            frame_rate: check_format.frame_rate.try_into().ok(),
        },
    })
}

fn resolve_str(context: &dyn ResolverContext, s: &str) -> anyhow::Result<Processor> {
    let cfg: crate::proto::cdcfg::Processor =
        protobuf::text_format::parse_from_str(s).context("protobuf text_format error")?;
    resolve(context, &cfg)
}

pub fn parse(context: &dyn ResolverContext, path: &Path) -> anyhow::Result<Processor> {
    let s = std::fs::read_to_string(path)
        .with_context(|| format!("read_to_string {}", path.display()))?;
    resolve_str(context, &s)
}

#[cfg(test)]
mod tests {
    use std::path::PathBuf;

    use super::resolve_str;
    use super::NaiveResolverContext;
    use crate::config::Processor;

    #[test]
    fn invalid_text_format() {
        let context: NaiveResolverContext = Default::default();
        let err = resolve_str(&context, "abcd").unwrap_err();
        assert!(
            err.to_string().contains("protobuf text_format error"),
            "{err}"
        );
    }

    #[test]
    fn passthrough() {
        let context: NaiveResolverContext = Default::default();
        let processor = resolve_str(
            &context,
            r#"pipeline {
  processors {
    wrap_chunk {
      inner_block_size: 10
      inner { resample { output_frame_rate: 24000 } }
    }
  }
  processors {
    plugin {
      path: "foo"
      constructor: "bar"
    }
  }
  processors {
    shuffle_channels {
        channel_indexes: 0
        channel_indexes: 1
    }
  }
  processors {
    check_format {
      channels: 2
      block_size: -1
      frame_rate: -1
    }
  }
}"#,
        )
        .unwrap();
        assert_eq!(
            processor,
            Processor::Pipeline {
                processors: vec![
                    Processor::WrapChunk {
                        inner: Box::new(Processor::Resample {
                            output_frame_rate: 24000
                        }),
                        inner_block_size: 10
                    },
                    Processor::Plugin {
                        path: PathBuf::from("foo"),
                        constructor: "bar".into(),
                    },
                    Processor::ShuffleChannels {
                        channel_indexes: vec![0, 1]
                    },
                    Processor::CheckFormat {
                        channels: Some(2),
                        block_size: None,
                        frame_rate: None
                    }
                ]
            }
        );
    }

    #[test]
    fn error_stack() {
        let context: NaiveResolverContext = Default::default();
        let err = resolve_str(
            &context,
            r#"pipeline {
  processors {
    shuffle_channels {
      channel_indexes: 0
      channel_indexes: 1
    }
  }
  processors {
    wrap_chunk {
      inner_block_size: 10
      inner {}
    }
  }
}"#,
        )
        .unwrap_err();

        assert_eq!(
            format!("{err:#}"),
            "pipeline processor 1: wrap_chunk inner: processor_oneof is empty"
        );
    }

    #[test]
    fn wav_dump() {
        let spec = r#"maybe_wav_dump { filename: "a.wav" }"#;

        let context: NaiveResolverContext = Default::default();
        let processor = resolve_str(&context, spec).unwrap();
        assert_eq!(processor, Processor::Nothing);

        let context = NaiveResolverContext {
            wav_dump_root: Some(PathBuf::from("/tmp")),
            ..Default::default()
        };
        let processor = resolve_str(&context, spec).unwrap();
        assert_eq!(
            processor,
            Processor::WavSink {
                path: PathBuf::from("/tmp/a.wav")
            }
        );
    }

    #[test]
    fn dlc() {
        let context: NaiveResolverContext = Default::default();
        let processor = resolve_str(
            &context,
            r#"dlc_plugin { dlc_id: "nc-ap-dlc" path: "libdenoiser.so" constructor: "plugin_processor_create" }"#,
        ).unwrap();
        assert_eq!(
            processor,
            Processor::Plugin {
                path: PathBuf::from("/run/imageloader/nc-ap-dlc/package/root/libdenoiser.so"),
                constructor: "plugin_processor_create".into()
            }
        );
    }

    #[test]
    fn duplicate_channel_0() {
        let spec = "maybe_duplicate_channel_0 {}";

        let context: NaiveResolverContext = Default::default();
        assert_eq!(resolve_str(&context, spec).unwrap(), Processor::Nothing);

        let context = NaiveResolverContext {
            duplicate_channel_0: Some(2),
            ..Default::default()
        };
        assert_eq!(
            resolve_str(&context, spec).unwrap(),
            Processor::ShuffleChannels {
                channel_indexes: vec![0; 2]
            }
        );
    }
}
