// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use std::path::Path;
use std::sync::atomic::AtomicUsize;
use std::sync::atomic::Ordering;

use anyhow::anyhow;
use anyhow::bail;
use anyhow::Context;
use audio_processor::processors::binding::plugin_processor;
use audio_processor::processors::export_plugin;
use audio_processor::processors::CheckShape;
use audio_processor::processors::ChunkWrapper;
use audio_processor::processors::DynamicPluginProcessor;
use audio_processor::processors::NegateAudioProcessor;
use audio_processor::processors::SpeexResampler;
use audio_processor::processors::ThreadedProcessor;
use audio_processor::AudioProcessor;
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
    Overridden,
}

#[repr(C)]
#[derive(Clone, Debug)]
pub struct CrasProcessorConfig {
    channels: usize,
    block_size: usize,
    frame_rate: usize,

    effect: CrasProcessorEffect,

    // Run the processor pipeline in a separate, dedicated thread.
    dedicated_thread: bool,
}

type Pipeline = Vec<Box<dyn AudioProcessor<I = f32, O = f32> + Send>>;

pub struct CrasProcessor {
    id: usize,
    check_shape: CheckShape<f32>,
    pipeline: Pipeline,
    _config: CrasProcessorConfig,
}

impl AudioProcessor for CrasProcessor {
    type I = f32;
    type O = f32;

    fn process<'a>(
        &'a mut self,
        mut input: audio_processor::MultiSlice<'a, Self::I>,
    ) -> audio_processor::Result<audio_processor::MultiSlice<'a, Self::O>> {
        input = self.check_shape.process(input)?;
        for processor in self.pipeline.iter_mut() {
            input = processor.process(input)?
        }
        Ok(input)
    }

    fn get_output_frame_rate<'a>(&'a self) -> usize {
        match self.pipeline.last() {
            Some(last) => last.get_output_frame_rate(),
            None => self._config.frame_rate,
        }
    }
}

static GLOBAL_ID_COUNTER: AtomicUsize = AtomicUsize::new(0);

fn create_noise_cancellation_pipeline(config: &CrasProcessorConfig) -> anyhow::Result<Pipeline> {
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

fn create_style_transfer_pipeline(config: &CrasProcessorConfig) -> anyhow::Result<Pipeline> {
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

impl CrasProcessor {
    fn new(mut config: CrasProcessorConfig) -> anyhow::Result<Self> {
        let override_config = processor_override::read_system_config().input;
        if override_config.enabled {
            config.effect = CrasProcessorEffect::Overridden;
        }
        let config = config;

        let id = GLOBAL_ID_COUNTER.fetch_add(1, Ordering::AcqRel);
        let check_shape = CheckShape::new(config.channels, config.block_size, config.frame_rate);
        let pipeline: Pipeline = match config.effect {
            CrasProcessorEffect::NoEffects => vec![],
            CrasProcessorEffect::Negate => vec![Box::new(NegateAudioProcessor::new(
                config.channels,
                config.block_size,
                config.frame_rate,
            ))],
            CrasProcessorEffect::NoiseCancellation => {
                create_noise_cancellation_pipeline(&config)
                    .context("failed when creating noise cancellation pipeline")?
            }
            CrasProcessorEffect::StyleTransfer => {
                let mut pipeline = Pipeline::new();
                let nc_pipeline = create_noise_cancellation_pipeline(&config)
                    .context("failed when creating noise cancellation pipeline")?;
                pipeline.extend(nc_pipeline);
                let style_pipeline = create_style_transfer_pipeline(&config)
                    .context("failed when creating style transfer pipeline")?;
                pipeline.extend(style_pipeline);
                pipeline
            }
            CrasProcessorEffect::Overridden => PluginLoader {
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
        };

        log::info!("CrasProcessor #{id} created with: {config:?}");
        Ok(CrasProcessor {
            id,
            check_shape,
            pipeline,
            _config: config,
        })
    }
}

// TODO(aaronyu): This is the wrong abstraction. We should have an API to
// chain processors with incompatible shapes and rates easily instead,
// when we have something that looks more like a graph.
struct PluginLoader<'a> {
    path: &'a str,
    constructor: &'a str,
    channels: usize,
    outer_rate: usize,
    inner_rate: usize,
    outer_block_size: usize,
    inner_block_size: usize,
    allow_chunk_wrapper: bool,
}

impl<'a> PluginLoader<'a> {
    fn load_and_wrap(self) -> anyhow::Result<Pipeline> {
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

        let pipeline: Pipeline = if self.outer_rate == self.inner_rate {
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

        Ok(pipeline)
    }
}

impl Drop for CrasProcessor {
    fn drop(&mut self) {
        log::info!("CrasProcessor #{} dropped", self.id);
    }
}

/// Create a CRAS processor.
///
/// # Safety
///
/// `config` must point to a CrasProcessorConfig struct.
/// `ret` is where the constructed plugin_processor would be stored
/// Returns true if the plugin_processor is successfully constructed,
/// returns false otherwise.
#[no_mangle]
pub unsafe extern "C" fn cras_processor_create(
    config: *const CrasProcessorConfig,
    ret: *mut *mut plugin_processor,
) -> bool {
    let config = match config.as_ref() {
        Some(config) => config,
        None => {
            *ret = std::ptr::null_mut();
            return false;
        }
    };

    let mut success = true;
    let processor = match CrasProcessor::new(config.clone()) {
        Ok(processor) => processor,
        Err(err) => {
            success = false;
            log::error!(
                "CrasProcessor::new failed with {:#}, creating no-op processor",
                err
            );

            let config = config.clone();
            CrasProcessor::new(CrasProcessorConfig {
                effect: CrasProcessorEffect::NoEffects,
                channels: config.channels,
                block_size: config.block_size,
                frame_rate: config.frame_rate,
                dedicated_thread: config.dedicated_thread,
            })
            .expect("CrasProcessor::new with CrasProcessorEffect::NoEffects should never fail")
        }
    };

    *ret = if config.dedicated_thread {
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
    return success;
}

/// Returns true if override is enabled in the system config file.
#[no_mangle]
pub extern "C" fn cras_processor_is_override_enabled() -> bool {
    processor_override::read_system_config().input.enabled
}
