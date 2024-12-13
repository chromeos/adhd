// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use std::path::Path;
use std::path::PathBuf;
use std::ptr::NonNull;
use std::sync::atomic::AtomicUsize;
use std::sync::atomic::Ordering;

use anyhow::ensure;
use anyhow::Context;
use audio_processor::cdcfg;
use audio_processor::cdcfg::ResolverContext;
use audio_processor::config::PipelineBuilder;
use audio_processor::config::PreloadedProcessor;
use audio_processor::config::Processor;
use audio_processor::processors::binding::plugin_processor;
use audio_processor::processors::export_plugin;
use audio_processor::processors::peer::AudioWorkerSubprocessFactory;
use audio_processor::processors::CheckShape;
use audio_processor::processors::PluginProcessor;
use audio_processor::processors::ThreadedProcessor;
use audio_processor::AudioProcessor;
use audio_processor::Format;
use audio_processor::Pipeline;
use cras_common::types_internal::CrasProcessorEffect;
use cras_dlc::get_dlc_state_cached;
use cras_s2::global::cras_s2_get_beamforming_config_path;
use cras_s2::global::cras_s2_get_cras_processor_vars;

mod processor_override;
mod proto;

#[repr(C)]
#[derive(Clone, Copy, Debug)]
pub enum CrasProcessorWrapMode {
    WrapModeNone,
    /// Run the processor pipeline in a separate, dedicated thread.
    WrapModeDedicatedThread,
    /// Run the processor pipeline with a ChunkWrapper.
    /// In this mode, the caller is allowed to run the pipeline with a block
    /// size that is different from `CrasProcessorConfig::block_size`
    WrapModeChunk,
}

#[repr(C)]
#[derive(Clone, Debug)]
pub struct CrasProcessorConfig {
    // The number of channels after apm_processor.
    channels: usize,
    block_size: usize,
    frame_rate: usize,

    effect: CrasProcessorEffect,

    wrap_mode: CrasProcessorWrapMode,

    // Enable processing dumps as WAVE files.
    wav_dump: bool,
}

impl CrasProcessorConfig {
    fn format(&self) -> Format {
        Format {
            channels: self.channels,
            block_size: self.block_size,
            frame_rate: self.frame_rate,
        }
    }
}

pub struct CrasProcessor {
    id: usize,
    pipeline: Pipeline,
    config: CrasProcessorConfig,
}

impl AudioProcessor for CrasProcessor {
    type I = f32;
    type O = f32;

    fn process<'a>(
        &'a mut self,
        input: audio_processor::MultiSlice<'a, Self::I>,
    ) -> audio_processor::Result<audio_processor::MultiSlice<'a, Self::O>> {
        self.pipeline.process(input)
    }

    fn get_output_format(&self) -> audio_processor::Format {
        self.pipeline.get_output_format()
    }
}

struct CrasProcessorResolverContext {
    wav_dump_root: Option<PathBuf>,
    cras_processor_format: Format,
}

impl audio_processor::cdcfg::ResolverContext for CrasProcessorResolverContext {
    fn get_wav_dump_root(&self) -> Option<&Path> {
        self.wav_dump_root.as_deref()
    }

    fn get_dlc_root_path(&self, dlc_id: &str) -> anyhow::Result<PathBuf> {
        let dlc_state = get_dlc_state_cached(dlc_id);
        ensure!(dlc_state.installed, "{dlc_id} not installed");
        Ok(dlc_state.root_path.into())
    }

    fn get_duplicate_channel_0(&self) -> Option<usize> {
        // Duplicate channel 0 to the cras_processor count when requested.
        Some(self.cras_processor_format.channels)
    }
}

static GLOBAL_ID_COUNTER: AtomicUsize = AtomicUsize::new(0);

fn get_noise_cancellation_pipeline_decl(
    context: &dyn ResolverContext,
) -> anyhow::Result<Processor> {
    cdcfg::parse(
        context,
        &cras_s2_get_cras_processor_vars(),
        Path::new("/etc/cras/processor/noise_cancellation.txtpb"),
    )
}

fn get_style_transfer_pipeline_decl(context: &dyn ResolverContext) -> anyhow::Result<Processor> {
    cdcfg::parse(
        context,
        &cras_s2_get_cras_processor_vars(),
        Path::new("/etc/cras/processor/style_transfer.txtpb"),
    )
}

fn get_beamforming_pipeline_decl(context: &dyn ResolverContext) -> anyhow::Result<Processor> {
    cdcfg::parse(
        context,
        &cras_s2_get_cras_processor_vars(),
        &cras_s2_get_beamforming_config_path().context("beamforming config path unknown")?,
    )
}

fn get_echo_pipeline_decl(context: &dyn ResolverContext) -> anyhow::Result<Processor> {
    cdcfg::parse(
        context,
        &cras_s2_get_cras_processor_vars(),
        Path::new("/etc/cras/processor/echo.txtpb"),
    )
}

impl CrasProcessor {
    fn new(
        mut config: CrasProcessorConfig,
        apm_processor: Option<PluginProcessor>,
    ) -> anyhow::Result<Self> {
        let override_config = processor_override::read_system_config().input;
        if override_config.enabled {
            config.effect = CrasProcessorEffect::Overridden;
        }
        let config = config;

        let id = GLOBAL_ID_COUNTER.fetch_add(1, Ordering::AcqRel);

        let dump_base = PathBuf::from(format!("/run/cras/debug/cras_processor_{id}"));

        let resolver_context = CrasProcessorResolverContext {
            wav_dump_root: if config.wav_dump {
                Some(dump_base.clone())
            } else {
                None
            },
            cras_processor_format: config.format(),
        };
        let mut decl = vec![];

        if config.wav_dump {
            std::fs::create_dir_all(&dump_base).context("mkdir dump_base")?;
            decl.push(Processor::WavSink {
                path: dump_base.join("input.wav"),
            });
        }

        if let Some(apm_processor) = apm_processor {
            decl.push(Processor::Preloaded(PreloadedProcessor {
                description: "apm",
                processor: Box::new(apm_processor),
            }));
            decl.push(Processor::Preloaded(PreloadedProcessor {
                description: "CheckShape",
                processor: Box::new(CheckShape::new(config.format())),
            }));

            if config.wav_dump {
                decl.push(Processor::WavSink {
                    path: dump_base.join("post_apm.wav"),
                });
            }
        }

        match config.effect {
            CrasProcessorEffect::NoEffects => {
                // Do nothing.
            }
            CrasProcessorEffect::Negate => {
                decl.push(Processor::Negate);
            }
            CrasProcessorEffect::NoiseCancellation | CrasProcessorEffect::StyleTransfer => {
                decl.push(Processor::ShuffleChannels {
                    channel_indexes: vec![0],
                });
                decl.push(
                    get_noise_cancellation_pipeline_decl(&resolver_context)
                        .context("failed get_noise_cancellation_pipeline_decl")?,
                );
                if let CrasProcessorEffect::StyleTransfer = config.effect {
                    decl.push(
                        get_style_transfer_pipeline_decl(&resolver_context)
                            .context("failed get_style_transfer_pipeline_decl")?,
                    );
                }
                decl.push(Processor::ShuffleChannels {
                    channel_indexes: vec![0; config.channels],
                });
            }
            CrasProcessorEffect::Beamforming => {
                decl.push(
                    get_beamforming_pipeline_decl(&resolver_context)
                        .context("failed when creating beamforming pipeline")?,
                );
            }
            CrasProcessorEffect::GenerateEcho => {
                decl.push(
                    get_echo_pipeline_decl(&resolver_context)
                        .context("failed when creating echo pipeline")?,
                );
            }
            CrasProcessorEffect::Overridden => {
                if override_config.frame_rate != 0 {
                    decl.push(Processor::Resample {
                        output_frame_rate: override_config.frame_rate as usize,
                    });
                }
                let plugin = Processor::Plugin {
                    path: override_config.plugin_path.clone().into(),
                    constructor: override_config.constructor.clone(),
                };
                decl.push(match override_config.block_size {
                    0 => plugin, // User existing block size if 0.
                    block_size => Processor::WrapChunk {
                        inner_block_size: block_size as usize,
                        inner: Box::new(plugin),
                        disallow_hoisting: false,
                    },
                });
            }
        };

        // Resample to input rate.
        decl.push(Processor::Resample {
            output_frame_rate: config.frame_rate,
        });

        // Check that the input format is the same as the output format.
        decl.push(Processor::CheckFormat {
            channels: Some(config.channels),
            block_size: Some(config.block_size),
            frame_rate: Some(config.frame_rate),
        });

        if config.wav_dump {
            decl.push(Processor::WavSink {
                path: dump_base.join("output.wav"),
            });
        }

        let pipeline = if matches!(config.wrap_mode, CrasProcessorWrapMode::WrapModeChunk) {
            Processor::WrapChunk {
                inner: Box::new(Processor::Pipeline { processors: decl }),
                inner_block_size: config.block_size,
                disallow_hoisting: true,
            }
        } else {
            Processor::Pipeline { processors: decl }
        };

        let decl_debug = format!("{pipeline:?}");
        let pipeline = PipelineBuilder::new(config.format())
            // TODO(b/349784210): Use a hardened worker factory.
            .with_worker_factory(AudioWorkerSubprocessFactory::default().with_set_thread_priority())
            .build(pipeline)
            .context("failed to build pipeline")?;

        log::info!("CrasProcessor #{id} created with: {config:?}");
        log::info!("CrasProcessor #{id} pipeline: {decl_debug}");

        Ok(CrasProcessor {
            id,
            pipeline,
            config,
        })
    }
}

impl Drop for CrasProcessor {
    fn drop(&mut self) {
        log::info!("CrasProcessor #{} dropped", self.id);
    }
}

#[repr(C)]
pub struct CrasProcessorCreateResult {
    /// The created processor.
    pub plugin_processor: *mut plugin_processor,
    /// The actual effect used in the processor.
    /// Might be different from what was passed to cras_processor_create.
    pub effect: CrasProcessorEffect,
}

impl CrasProcessorCreateResult {
    fn none() -> Self {
        Self {
            plugin_processor: std::ptr::null_mut(),
            effect: CrasProcessorEffect::NoEffects,
        }
    }
}

unsafe fn create_apm_processor(
    config: &CrasProcessorConfig,
    apm_plugin_processor: *mut plugin_processor,
) -> anyhow::Result<Option<PluginProcessor>> {
    let apm_processor = match NonNull::new(apm_plugin_processor) {
        Some(apm_plugin_processor) => Some(
            PluginProcessor::from_handle(apm_plugin_processor.as_ptr(), config.format())
                .context("failed PluginProcessor::from_handle")?,
        ),
        None => None,
    };
    Ok(apm_processor)
}

/// Create a CRAS processor.
///
/// Returns the created processor (might be NULL), and the applied effect.
///
/// # Safety
///
/// `config` must point to a CrasProcessorConfig struct.
/// `apm_plugin_processor` must point to a plugin_processor or NULL.
#[no_mangle]
pub unsafe extern "C" fn cras_processor_create(
    config: *const CrasProcessorConfig,
    apm_plugin_processor: *mut plugin_processor,
) -> CrasProcessorCreateResult {
    let config = match config.as_ref() {
        Some(config) => config,
        None => {
            return CrasProcessorCreateResult::none();
        }
    };

    let apm_processor = match create_apm_processor(config, apm_plugin_processor) {
        Ok(apm_processor) => apm_processor,
        Err(err) => {
            log::error!("{err:#}");
            return CrasProcessorCreateResult::none();
        }
    };

    let processor = match CrasProcessor::new(config.clone(), apm_processor) {
        Ok(processor) => processor,
        Err(err) => {
            log::error!(
                "CrasProcessor::new failed with {:#}, creating no-op processor",
                err
            );

            let config = config.clone();
            CrasProcessor::new(
                CrasProcessorConfig {
                    effect: CrasProcessorEffect::NoEffects,
                    ..config
                },
                // apm_processor was consumed so create it again.
                create_apm_processor(&config, apm_plugin_processor).expect("create_apm_processor should not fail given that we created it successfully once"),
            )
            .expect("CrasProcessor::new with CrasProcessorEffect::NoEffects should never fail")
        }
    };

    let effect = processor.config.effect;
    let plugin_processor = if matches!(
        config.wrap_mode,
        CrasProcessorWrapMode::WrapModeDedicatedThread
    ) {
        let threaded_processor = ThreadedProcessor::new(processor, 1);
        export_plugin(threaded_processor)
    } else {
        export_plugin(processor)
    };
    CrasProcessorCreateResult {
        plugin_processor,
        effect,
    }
}

/// Returns true if override is enabled in the system config file.
#[no_mangle]
pub extern "C" fn cras_processor_is_override_enabled() -> bool {
    processor_override::read_system_config().input.enabled
}
