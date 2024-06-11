// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use std::path::Path;
use std::path::PathBuf;
use std::ptr::NonNull;
use std::sync::atomic::AtomicUsize;
use std::sync::atomic::Ordering;

use anyhow::bail;
use anyhow::Context;
use audio_processor::config::build_pipeline;
use audio_processor::config::PreloadedProcessor;
use audio_processor::config::Processor;
use audio_processor::processors::binding::plugin_processor;
use audio_processor::processors::export_plugin;
use audio_processor::processors::CheckShape;
use audio_processor::processors::PluginProcessor;
use audio_processor::processors::ThreadedProcessor;
use audio_processor::AudioProcessor;
use audio_processor::Format;
use audio_processor::ProcessorVec;
use cras_dlc::get_dlc_state_cached;

mod processor_override;

#[repr(C)]
#[derive(Clone, Copy, Debug)]
pub enum CrasProcessorEffect {
    NoEffects,
    Negate,
    NoiseCancellation,
    StyleTransfer,
    Beamforming,
    Overridden,
}

#[repr(C)]
#[derive(Clone, Debug)]
pub struct CrasProcessorConfig {
    // The number of channels after apm_processor.
    channels: usize,
    block_size: usize,
    frame_rate: usize,

    effect: CrasProcessorEffect,

    // Run the processor pipeline in a separate, dedicated thread.
    dedicated_thread: bool,

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
    pipeline: ProcessorVec,
    config: CrasProcessorConfig,
}

impl AudioProcessor for CrasProcessor {
    type I = f32;
    type O = f32;

    fn process<'a>(
        &'a mut self,
        mut input: audio_processor::MultiSlice<'a, Self::I>,
    ) -> audio_processor::Result<audio_processor::MultiSlice<'a, Self::O>> {
        for processor in self.pipeline.iter_mut() {
            input = processor.process(input)?
        }
        Ok(input)
    }

    fn get_output_format(&self) -> audio_processor::Format {
        self.pipeline.get_output_format()
    }
}

static GLOBAL_ID_COUNTER: AtomicUsize = AtomicUsize::new(0);

fn get_noise_cancellation_pipeline_decl() -> anyhow::Result<Vec<Processor>> {
    let ap_nc_dlc = get_dlc_state_cached(cras_dlc::CrasDlcId::CrasDlcNcAp);
    if !ap_nc_dlc.installed {
        bail!("{} not installed", cras_dlc::CrasDlcId::CrasDlcNcAp);
    }

    Ok(vec![
        Processor::Resample {
            output_frame_rate: 48000,
        },
        Processor::CheckFormat {
            channels: Some(1),
            block_size: Some(480),
            frame_rate: Some(48000),
        },
        Processor::Plugin {
            path: Path::new(&ap_nc_dlc.root_path).join("libdenoiser.so"),
            constructor: "plugin_processor_create".into(),
        },
    ])
}

fn get_style_transfer_pipeline_decl() -> anyhow::Result<Vec<Processor>> {
    let nuance_dlc = get_dlc_state_cached(cras_dlc::CrasDlcId::CrasDlcNuance);
    if !nuance_dlc.installed {
        bail!("{} not installed", cras_dlc::CrasDlcId::CrasDlcNuance);
    }

    Ok(vec![
        Processor::CheckFormat {
            channels: Some(1),
            block_size: None,
            frame_rate: None,
        },
        Processor::Resample {
            output_frame_rate: 24000,
        },
        Processor::WrapChunk {
            inner_block_size: 480,
            inner: Box::new(Processor::Plugin {
                path: Path::new(&nuance_dlc.root_path).join("libstyle.so"),
                constructor: "plugin_processor_create_ast".into(),
            }),
        },
    ])
}

struct PluginDumpConfig {
    pre_processing_wav_dump: PathBuf,
    post_processing_wav_dump: PathBuf,
}

fn get_beamforming_pipeline_decl(
    dump_config: Option<PluginDumpConfig>,
) -> anyhow::Result<Vec<Processor>> {
    let plugin = Processor::Plugin {
        path: "/usr/local/lib64/libigo_processor.so".into(),
        constructor: "plugin_processor_create".into(),
    };
    let inner = match dump_config {
        Some(dump_config) => Processor::Pipeline {
            processors: vec![
                Processor::WavSink {
                    path: dump_config.pre_processing_wav_dump,
                },
                plugin,
                Processor::WavSink {
                    path: dump_config.post_processing_wav_dump,
                },
            ],
        },
        None => plugin,
    };
    Ok(vec![
        Processor::CheckFormat {
            channels: Some(3),
            block_size: None,
            frame_rate: None,
        },
        Processor::Resample {
            output_frame_rate: 16000,
        },
        Processor::WrapChunk {
            inner_block_size: 256,
            inner: Box::new(inner),
        },
        Processor::ShuffleChannels {
            channel_indexes: vec![0; 3],
        },
    ])
}

impl CrasProcessor {
    fn new(
        mut config: CrasProcessorConfig,
        apm_processor: PluginProcessor,
    ) -> anyhow::Result<Self> {
        let override_config = processor_override::read_system_config().input;
        if override_config.enabled {
            config.effect = CrasProcessorEffect::Overridden;
        }
        let config = config;

        let id = GLOBAL_ID_COUNTER.fetch_add(1, Ordering::AcqRel);

        let dump_base = PathBuf::from(format!("/run/cras/debug/cras_processor_{id}"));

        let mut decl = vec![];

        if config.wav_dump {
            std::fs::create_dir_all(&dump_base).context("mkdir dump_base")?;
            decl.push(Processor::WavSink {
                path: dump_base.join("input.wav"),
            });
        }

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
                decl.extend(
                    get_noise_cancellation_pipeline_decl()
                        .context("failed get_noise_cancellation_pipeline_decl")?,
                );
                if let CrasProcessorEffect::StyleTransfer = config.effect {
                    decl.extend(
                        get_style_transfer_pipeline_decl()
                            .context("failed get_style_transfer_pipeline_decl")?,
                    );
                }
                decl.push(Processor::ShuffleChannels {
                    channel_indexes: vec![0; config.channels],
                });
            }
            CrasProcessorEffect::Beamforming => {
                let dump_config = if config.wav_dump {
                    Some(PluginDumpConfig {
                        pre_processing_wav_dump: dump_base.join("pre_beamforming.wav"),
                        post_processing_wav_dump: dump_base.join("post_beamforming.wav"),
                    })
                } else {
                    None
                };
                decl.extend(
                    get_beamforming_pipeline_decl(dump_config)
                        .context("failed when creating beamforming pipeline")?,
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

        let decl_debug = format!("{decl:?}");
        let pipeline = build_pipeline(config.format(), Processor::Pipeline { processors: decl })
            .context("build_pipeline failed")?;

        log::info!("CrasProcessor #{id} created with: {config:?}");
        log::info!("CrasProcessor #{id} pipeline: {decl_debug:?}");

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

/// Create a CRAS processor.
///
/// Returns the created processor (might be NULL), and the applied effect.
///
/// # Safety
///
/// `config` must point to a CrasProcessorConfig struct.
/// `apm_plugin_processor` must point to a plugin_processor.
#[no_mangle]
pub unsafe extern "C" fn cras_processor_create(
    config: *const CrasProcessorConfig,
    apm_plugin_processor: NonNull<plugin_processor>,
) -> CrasProcessorCreateResult {
    let config = match config.as_ref() {
        Some(config) => config,
        None => {
            return CrasProcessorCreateResult::none();
        }
    };

    let apm_processor =
        match PluginProcessor::from_handle(apm_plugin_processor.as_ptr(), config.format()) {
            Ok(processor) => processor,
            Err(err) => {
                log::error!("failed PluginProcessor::from_handle {:#}", err);
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
                    channels: config.channels,
                    block_size: config.block_size,
                    frame_rate: config.frame_rate,
                    dedicated_thread: config.dedicated_thread,
                    wav_dump: config.wav_dump,
                },
                // apm_processor was consumed so create it again.
                // It should not fail given that we created it successfully once.
                PluginProcessor::from_handle(apm_plugin_processor.as_ptr(), config.format())
                    .expect("PluginProcessor::from_handle failed"),
            )
            .expect("CrasProcessor::new with CrasProcessorEffect::NoEffects should never fail")
        }
    };

    let effect = processor.config.effect;
    let plugin_processor = if config.dedicated_thread {
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
