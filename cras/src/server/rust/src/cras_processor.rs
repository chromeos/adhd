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
use audio_processor::processors::binding::plugin_processor;
use audio_processor::processors::export_plugin;
use audio_processor::processors::CheckShape;
use audio_processor::processors::NegateAudioProcessor;
use audio_processor::processors::PluginLoader;
use audio_processor::processors::PluginProcessor;
use audio_processor::processors::ShuffleChannels;
use audio_processor::processors::ThreadedProcessor;
use audio_processor::AudioProcessor;
use audio_processor::Pipeline;
use audio_processor::ProcessorVec;
use audio_processor::Shape;
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

    fn get_output_frame_rate<'a>(&'a self) -> usize {
        match self.pipeline.last() {
            Some(last) => last.get_output_frame_rate(),
            None => self.config.frame_rate,
        }
    }
}

static GLOBAL_ID_COUNTER: AtomicUsize = AtomicUsize::new(0);

fn create_noise_cancellation_pipeline(
    config: &CrasProcessorConfig,
) -> anyhow::Result<ProcessorVec> {
    // Check shape is supported.
    if config.channels != 1 {
        bail!("unsupported channel count {}", config.channels);
    }
    if config.block_size * 100 != config.frame_rate {
        bail!("config is not using a block size of 10ms: {:?}", config);
    }

    let ap_nc_dlc = get_dlc_state_cached(cras_dlc::CrasDlcId::CrasDlcNcAp);
    if !ap_nc_dlc.installed {
        bail!("{} not installed", cras_dlc::CrasDlcId::CrasDlcNcAp);
    }

    PluginLoader {
        path: Path::new(&ap_nc_dlc.root_path)
            .join("libdenoiser.so")
            .to_str()
            .unwrap(),
        constructor: "plugin_processor_create",
        channels: config.channels,
        outer_rate: config.frame_rate,
        inner_rate: 48000,
        outer_block_size: config.block_size,
        inner_block_size: 480,
        allow_chunk_wrapper: false,
    }
    .load_and_wrap()
}

fn create_style_transfer_pipeline(config: &CrasProcessorConfig) -> anyhow::Result<ProcessorVec> {
    // Check shape is supported.
    if config.channels != 1 {
        bail!("unsupported channel count {}", config.channels);
    }

    let nuance_dlc = get_dlc_state_cached(cras_dlc::CrasDlcId::CrasDlcNuance);
    if !nuance_dlc.installed {
        bail!("{} not installed", cras_dlc::CrasDlcId::CrasDlcNuance);
    }

    PluginLoader {
        path: Path::new(&nuance_dlc.root_path)
            .join("libstyle.so")
            .to_str()
            .unwrap(),
        constructor: "plugin_processor_create_ast",
        channels: config.channels,
        outer_rate: config.frame_rate,
        inner_rate: 24000,
        outer_block_size: config.block_size,
        inner_block_size: 480,
        allow_chunk_wrapper: true,
    }
    .load_and_wrap()
}

fn create_beamforming_pipeline(config: &CrasProcessorConfig) -> anyhow::Result<ProcessorVec> {
    // Check shape is supported.
    if config.channels != 3 {
        bail!("unsupported channel count {}", config.channels);
    }

    PluginLoader {
        // TODO(aaronyu): Use DLC instead.
        path: "/usr/local/lib64/libigo_processor.so",
        constructor: "plugin_processor_create",
        channels: config.channels,
        outer_rate: config.frame_rate,
        inner_rate: 16000,
        outer_block_size: config.block_size,
        inner_block_size: 256,
        allow_chunk_wrapper: true,
    }
    .load_and_wrap()
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

        let mut pipeline = vec![];

        if config.wav_dump {
            std::fs::create_dir_all(&dump_base).context("mkdir dump_base")?;
            pipeline.add_wav_dump(
                &dump_base.join("input.wav"),
                config.channels,
                config.frame_rate,
            )?;
        }

        pipeline.add(apm_processor);
        pipeline.add(CheckShape::new(
            config.channels,
            config.block_size,
            config.frame_rate,
        ));

        if config.wav_dump {
            pipeline.add_wav_dump(
                &dump_base.join("post_apm.wav"),
                config.channels,
                config.frame_rate,
            )?;
        }

        match config.effect {
            CrasProcessorEffect::NoEffects => {
                // Do nothing.
            }
            CrasProcessorEffect::Negate => {
                pipeline.add(NegateAudioProcessor::new(
                    config.channels,
                    config.block_size,
                    config.frame_rate,
                ));
            }
            CrasProcessorEffect::NoiseCancellation | CrasProcessorEffect::StyleTransfer => {
                if config.channels > 1 {
                    // Run mono noise cancellation.
                    // Pick just the first channel.
                    pipeline.add(ShuffleChannels::new(
                        &[0],
                        Shape {
                            channels: config.channels,
                            frames: config.block_size,
                        },
                        config.frame_rate,
                    ));
                }
                // TODO: Change this to an audio format struct when we have it.
                let mono_config = CrasProcessorConfig {
                    channels: 1,
                    ..config
                };
                pipeline.extend(
                    create_noise_cancellation_pipeline(&mono_config)
                        .context("failed when creating noise cancellation pipeline")?,
                );
                if let CrasProcessorEffect::StyleTransfer = config.effect {
                    pipeline.extend(
                        create_style_transfer_pipeline(&mono_config)
                            .context("failed when creating style transfer pipeline")?,
                    );
                }
                if config.channels > 1 {
                    // Copy to all channels.
                    pipeline.add(ShuffleChannels::new(
                        &vec![0; config.channels],
                        Shape {
                            channels: config.channels,
                            frames: config.block_size,
                        },
                        config.frame_rate,
                    ));
                }
            }
            CrasProcessorEffect::Beamforming => {
                pipeline.extend(
                    create_beamforming_pipeline(&config)
                        .context("failed when creating beamforming pipeline")?,
                );
            }
            CrasProcessorEffect::Overridden => {
                pipeline.extend(
                    PluginLoader {
                        path: &override_config.plugin_path,
                        constructor: &override_config.constructor,
                        channels: config.channels,
                        outer_rate: config.frame_rate,
                        inner_rate: match override_config.frame_rate {
                            0 => config.frame_rate, // Use outer rate if 0.
                            rate => rate as usize,
                        },
                        outer_block_size: config.block_size,
                        inner_block_size: match override_config.block_size {
                            0 => config.block_size, // User outer block size if 0.
                            block_size => block_size as usize,
                        },
                        allow_chunk_wrapper: true,
                    }
                    .load_and_wrap()?,
                );
            }
        };

        if config.wav_dump {
            pipeline.add_wav_dump(
                &dump_base.join("output.wav"),
                config.channels,
                config.frame_rate,
            )?;
        }

        log::info!("CrasProcessor #{id} created with: {config:?}");
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
    let apm_processor = match PluginProcessor::from_handle(apm_plugin_processor.as_ptr()) {
        Ok(processor) => processor,
        Err(err) => {
            log::error!("failed PluginProcessor::from_handle {:#}", err);
            return CrasProcessorCreateResult::none();
        }
    };

    let config = match config.as_ref() {
        Some(config) => config,
        None => {
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
                PluginProcessor::from_handle(apm_plugin_processor.as_ptr())
                    .expect("PluginProcessor::from_handle failed"),
            )
            .expect("CrasProcessor::new with CrasProcessorEffect::NoEffects should never fail")
        }
    };

    let effect = processor.config.effect;
    let plugin_processor = if config.dedicated_thread {
        let threaded_processor = ThreadedProcessor::new(
            processor,
            Shape {
                channels: config.channels,
                frames: config.block_size,
            },
            1,
        );
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
