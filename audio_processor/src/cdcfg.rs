// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use std::cell::RefCell;
use std::collections::HashMap;
use std::collections::HashSet;
use std::path::Path;
use std::path::PathBuf;

use anyhow::bail;
use anyhow::Context;

use crate::config::Processor;
use crate::proto::cdcfg::dlc_plugin;
use crate::proto::cdcfg::dlc_plugin::Dlc_id_oneof;
use crate::proto::cdcfg::plugin;

pub trait ResolverContext {
    fn get_wav_dump_root(&self) -> Option<&Path>;
    fn get_dlc_root_path(&self, dlc_id: &str) -> anyhow::Result<PathBuf>;
    fn get_duplicate_channel_0(&self) -> Option<usize>;
}

#[derive(Default)]
pub struct NaiveResolverContext {
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

#[derive(Default)]
struct DlcIdCollector {
    dlcs: RefCell<HashSet<String>>,
}

impl ResolverContext for DlcIdCollector {
    fn get_wav_dump_root(&self) -> Option<&Path> {
        None
    }

    fn get_dlc_root_path(&self, dlc_id: &str) -> anyhow::Result<PathBuf> {
        self.dlcs.borrow_mut().insert(dlc_id.to_string());
        // DlcIdCollector is a stub ResolverContext used only to figure out
        // the required DLCs, we don't have to return the real DLC path.
        Ok(PathBuf::from("fake-dlc-path"))
    }

    fn get_duplicate_channel_0(&self) -> Option<usize> {
        None
    }
}

enum ConstOrVar<'a> {
    Const(&'a str),
    Var(&'a str),
}

impl<'a> From<&'a Dlc_id_oneof> for ConstOrVar<'a> {
    fn from(value: &'a Dlc_id_oneof) -> Self {
        match value {
            Dlc_id_oneof::DlcId(s) => ConstOrVar::Const(s),
            Dlc_id_oneof::DlcIdVar(s) => ConstOrVar::Var(s),
        }
    }
}

impl<'a> From<&'a dlc_plugin::Path_oneof> for ConstOrVar<'a> {
    fn from(value: &'a dlc_plugin::Path_oneof) -> Self {
        match value {
            dlc_plugin::Path_oneof::Path(s) => ConstOrVar::Const(s),
            dlc_plugin::Path_oneof::PathVar(s) => ConstOrVar::Var(s),
        }
    }
}

impl<'a> From<&'a dlc_plugin::Constructor_oneof> for ConstOrVar<'a> {
    fn from(value: &'a dlc_plugin::Constructor_oneof) -> Self {
        match value {
            dlc_plugin::Constructor_oneof::Constructor(s) => ConstOrVar::Const(s),
            dlc_plugin::Constructor_oneof::ConstructorVar(s) => ConstOrVar::Var(s),
        }
    }
}

impl<'a> From<&'a plugin::Path_oneof> for ConstOrVar<'a> {
    fn from(value: &'a plugin::Path_oneof) -> Self {
        match value {
            plugin::Path_oneof::Path(s) => ConstOrVar::Const(s),
            plugin::Path_oneof::PathVar(s) => ConstOrVar::Var(s),
        }
    }
}

impl<'a> From<&'a plugin::Constructor_oneof> for ConstOrVar<'a> {
    fn from(value: &'a plugin::Constructor_oneof) -> Self {
        match value {
            plugin::Constructor_oneof::Constructor(s) => ConstOrVar::Const(s),
            plugin::Constructor_oneof::ConstructorVar(s) => ConstOrVar::Var(s),
        }
    }
}

fn get_const_or_var_str<'a>(
    vars: &'a HashMap<String, String>,
    const_or_var: ConstOrVar<'a>,
) -> anyhow::Result<&'a str> {
    match const_or_var {
        ConstOrVar::Const(s) => Ok(s),
        ConstOrVar::Var(s) => Ok(vars
            .get(s)
            .with_context(|| format!("var {s:?} not found"))?),
    }
}

fn resolve(
    context: &dyn ResolverContext,
    vars: &HashMap<String, String>,
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
            path: Path::new(get_const_or_var_str(
                vars,
                plugin
                    .path_oneof
                    .as_ref()
                    .context("missing plugin path")?
                    .into(),
            )?)
            .into(),
            constructor: get_const_or_var_str(
                vars,
                plugin
                    .constructor_oneof
                    .as_ref()
                    .context("missing constructor")?
                    .into(),
            )?
            .to_string(),
        },
        DlcPlugin(dlc_plugin) => Processor::Plugin {
            path: context
                .get_dlc_root_path(get_const_or_var_str(
                    vars,
                    dlc_plugin
                        .dlc_id_oneof
                        .as_ref()
                        .context("missing dlc_id")?
                        .into(),
                )?)
                .context("context.dlc_root_path")?
                .join(get_const_or_var_str(
                    vars,
                    dlc_plugin
                        .path_oneof
                        .as_ref()
                        .context("missing path")?
                        .into(),
                )?),
            constructor: get_const_or_var_str(
                vars,
                dlc_plugin
                    .constructor_oneof
                    .as_ref()
                    .context("missing constructor")?
                    .into(),
            )?
            .to_string(),
        },
        WrapChunk(wrap_chunk) => Processor::WrapChunk {
            inner: Box::new(resolve(context, vars, &wrap_chunk.inner).context("wrap_chunk inner")?),
            inner_block_size: wrap_chunk
                .inner_block_size
                .try_into()
                .context("wrap_chunk inner_block_size")?,
            disallow_hoisting: false,
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
                    resolve(context, vars, processor)
                        .with_context(|| format!("pipeline processor {i}"))
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
            channels: positive_or_none(check_format.channels),
            block_size: positive_or_none(check_format.block_size),
            frame_rate: positive_or_none(check_format.frame_rate),
        },
        Peer(peer) => Processor::Peer {
            processor: Box::new(resolve(context, vars, &peer.processor).context("peer.processor")?),
        },
    })
}

fn positive_or_none(val: i32) -> Option<usize> {
    match val.try_into() {
        Ok(0) => None,
        Ok(val) => Some(val),
        Err(_) => None,
    }
}

fn resolve_str(
    context: &dyn ResolverContext,
    vars: &HashMap<String, String>,
    s: &str,
) -> anyhow::Result<Processor> {
    let cfg: crate::proto::cdcfg::Processor =
        protobuf::text_format::parse_from_str(s).context("protobuf text_format error")?;
    resolve(context, vars, &cfg)
}

pub fn parse(
    context: &dyn ResolverContext,
    vars: &HashMap<String, String>,
    path: &Path,
) -> anyhow::Result<Processor> {
    let s = std::fs::read_to_string(path)
        .with_context(|| format!("read_to_string {}", path.display()))?;
    resolve_str(context, vars, &s)
}

/// Get all DLC IDs specified in a given processing config.
pub fn get_required_dlcs(
    vars: &HashMap<String, String>,
    path: &Path,
) -> anyhow::Result<HashSet<String>> {
    let resolver = DlcIdCollector::default();
    parse(&resolver, vars, path)?;
    Ok(resolver.dlcs.take())
}

#[cfg(test)]
mod tests {
    use std::collections::HashMap;
    use std::collections::HashSet;
    use std::path::PathBuf;

    use super::resolve_str;
    use super::NaiveResolverContext;
    use super::ResolverContext;
    use crate::cdcfg::DlcIdCollector;
    use crate::config::Processor;

    fn assert_resolve_error(
        context: &dyn ResolverContext,
        vars: &HashMap<String, String>,
        s: &str,
        err_substring: &str,
    ) {
        let err = resolve_str(context, vars, s).unwrap_err();
        assert!(
            err.to_string().contains(err_substring),
            "{:?} does not contain {err_substring}",
            err.to_string()
        );
    }

    #[test]
    fn invalid_text_format() {
        let context: NaiveResolverContext = Default::default();
        assert_resolve_error(
            &context,
            &HashMap::new(),
            "abcd",
            "protobuf text_format error",
        );
    }

    #[test]
    fn passthrough() {
        let context: NaiveResolverContext = Default::default();
        let processor = resolve_str(
            &context,
            &HashMap::new(),
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
      block_size: 0
      frame_rate: -1
    }
  }
  processors {
    peer {
      processor {
        resample { output_frame_rate: 48000 }
      }
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
                        inner_block_size: 10,
                        disallow_hoisting: false,
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
                    },
                    Processor::Peer {
                        processor: Box::new(Processor::Resample {
                            output_frame_rate: 48000
                        })
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
            &HashMap::new(),
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
        let processor = resolve_str(&context, &HashMap::new(), spec).unwrap();
        assert_eq!(processor, Processor::Nothing);

        let context = NaiveResolverContext {
            wav_dump_root: Some(PathBuf::from("/tmp")),
            ..Default::default()
        };
        let processor = resolve_str(&context, &HashMap::new(), spec).unwrap();
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
        let spec = r#"
pipeline {
  processors {
    dlc_plugin { dlc_id: "nc-ap-dlc" path: "libdenoiser.so" constructor: "plugin_processor_create" }
  }
  processors {
    dlc_plugin { dlc_id: "second-dlc" path: "libsecond.so" constructor: "plugin_processor_create2" }
  }
}"#;
        let processor = resolve_str(&context, &HashMap::new(), spec).unwrap();
        assert_eq!(
            processor,
            Processor::Pipeline {
                processors: vec![
                    Processor::Plugin {
                        path: PathBuf::from(
                            "/run/imageloader/nc-ap-dlc/package/root/libdenoiser.so"
                        ),
                        constructor: "plugin_processor_create".into(),
                    },
                    Processor::Plugin {
                        path: PathBuf::from(
                            "/run/imageloader/second-dlc/package/root/libsecond.so"
                        ),
                        constructor: "plugin_processor_create2".into(),
                    }
                ]
            }
        );

        let context = DlcIdCollector::default();
        resolve_str(&context, &HashMap::new(), spec).unwrap();
        assert_eq!(
            context.dlcs.borrow().clone(),
            HashSet::from(["nc-ap-dlc".to_string(), "second-dlc".to_string()])
        );
    }

    #[test]
    fn dlc_missing_fields() {
        let context: NaiveResolverContext = Default::default();
        assert_resolve_error(
            &context,
            &HashMap::new(),
            r#"dlc_plugin { path: "libdenoiser.so" constructor: "plugin_processor_create" }"#,
            "missing dlc_id",
        );
        assert_resolve_error(
            &context,
            &HashMap::new(),
            r#"dlc_plugin { dlc_id: "nc-ap-dlc" constructor: "plugin_processor_create" }"#,
            "missing path",
        );
        assert_resolve_error(
            &context,
            &HashMap::new(),
            r#"dlc_plugin { dlc_id: "nc-ap-dlc" path: "libdenoiser.so" }"#,
            "missing constructor",
        );
    }

    #[test]
    fn dlc_var() {
        let context: NaiveResolverContext = Default::default();
        let vars = HashMap::from_iter(
            [("a", "aaa"), ("b", "bbb"), ("c", "ccc")]
                .iter()
                .map(|(k, v)| (k.to_string(), v.to_string())),
        );
        let spec = r#"dlc_plugin { dlc_id_var: "a" path_var: "b" constructor_var: "c" }"#;
        let processor = resolve_str(&context, &vars, spec).unwrap();
        assert_eq!(
            processor,
            Processor::Plugin {
                path: PathBuf::from("/run/imageloader/aaa/package/root/bbb"),
                constructor: "ccc".into(),
            }
        );
    }

    #[test]
    fn dlc_var_missing() {
        let context: NaiveResolverContext = Default::default();
        assert_resolve_error(
            &context,
            &HashMap::new(),
            r#"dlc_plugin { dlc_id_var: "xyz" path: "libdenoiser.so" constructor: "plugin_processor_create" }"#,
            r#"var "xyz" not found"#,
        );
    }

    #[test]
    fn plugin_var() {
        let context: NaiveResolverContext = Default::default();
        let vars = HashMap::from_iter(
            [("a", "aaa"), ("b", "/path/to/bbb"), ("c", "ccc")]
                .iter()
                .map(|(k, v)| (k.to_string(), v.to_string())),
        );
        let spec = r#"plugin { path_var: "b" constructor_var: "c" }"#;
        let processor = resolve_str(&context, &vars, spec).unwrap();
        assert_eq!(
            processor,
            Processor::Plugin {
                path: PathBuf::from("/path/to/bbb"),
                constructor: "ccc".into(),
            }
        );
    }

    #[test]
    fn plugin_var_missing() {
        let context: NaiveResolverContext = Default::default();
        assert_resolve_error(
            &context,
            &HashMap::new(),
            r#"plugin { path_var: "xyz"constructor: "plugin_processor_create" }"#,
            r#"var "xyz" not found"#,
        );
    }

    #[test]
    fn duplicate_channel_0() {
        let spec = "maybe_duplicate_channel_0 {}";

        let context: NaiveResolverContext = Default::default();
        assert_eq!(
            resolve_str(&context, &HashMap::new(), spec).unwrap(),
            Processor::Nothing
        );

        let context = NaiveResolverContext {
            duplicate_channel_0: Some(2),
            ..Default::default()
        };
        assert_eq!(
            resolve_str(&context, &HashMap::new(), spec).unwrap(),
            Processor::ShuffleChannels {
                channel_indexes: vec![0; 2]
            }
        );
    }
}
