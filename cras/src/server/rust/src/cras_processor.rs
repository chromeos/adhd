// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use std::path::Path;

use anyhow::anyhow;
use anyhow::Context;
use audio_processor::processors::binding::plugin_processor;
use audio_processor::processors::export_plugin;
use audio_processor::processors::CheckShape;
use audio_processor::processors::DynamicPluginProcessor;
use audio_processor::processors::NegateAudioProcessor;
use audio_processor::processors::SpeexResampler;
use audio_processor::AudioProcessor;
use audio_processor::Shape;
use cras_dlc::get_dlc_state;

#[repr(C)]
#[derive(Clone, Copy, Debug)]
pub enum CrasProcessorEffect {
    NoEffects,
    Negate,
    NoiseCancellation,
}

#[repr(C)]
#[derive(Clone, Debug)]
pub struct CrasProcessorConfig {
    channels: usize,
    block_size: usize,
    frame_rate: usize,

    effect: CrasProcessorEffect,
}

pub struct CrasProcessor {
    check_shape: CheckShape<f32>,
    pipeline: Vec<Box<dyn AudioProcessor<I = f32, O = f32>>>,
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
}

impl CrasProcessor {
    fn new(config: CrasProcessorConfig) -> anyhow::Result<Self> {
        let check_shape = CheckShape::new(config.channels, config.block_size);
        let pipeline: Vec<Box<dyn AudioProcessor<I = f32, O = f32>>> = match config.effect {
            CrasProcessorEffect::NoEffects => vec![],
            CrasProcessorEffect::Negate => vec![Box::new(NegateAudioProcessor::new(
                config.channels,
                config.block_size,
            ))],
            CrasProcessorEffect::NoiseCancellation => {
                // Check shape is supported.
                if config.channels != 1 {
                    return Err(anyhow!("unsupported channel count {}", config.channels));
                }
                if config.block_size * 100 != config.frame_rate {
                    return Err(anyhow!(
                        "config is not using a block size of 10ms: {:?}",
                        config
                    ));
                }

                let ap_nc_dlc = get_dlc_state(cras_dlc::CrasDlcId::CrasDlcNcAp)?;
                if !ap_nc_dlc.installed {
                    return Err(anyhow!(
                        "{} not installed",
                        cras_dlc::CrasDlcId::CrasDlcNcAp
                    ));
                }

                let processor = DynamicPluginProcessor::new(
                    Path::new(&ap_nc_dlc.root_path)
                        .join("libdenoiser.so")
                        .to_str()
                        .unwrap(),
                    "plugin_processor_create",
                    480,
                    config.channels,
                    48000,
                )
                .with_context(|| "DynamicPluginProcessor::new failed")?;

                if config.frame_rate == 48000 {
                    vec![Box::new(processor)]
                } else {
                    vec![
                        Box::new(
                            SpeexResampler::new(
                                Shape {
                                    channels: config.channels,
                                    frames: config.block_size,
                                },
                                config.frame_rate,
                                48000,
                            )
                            .with_context(|| "failed to create 1st wrapping resampler")?,
                        ),
                        Box::new(processor),
                        Box::new(
                            SpeexResampler::new(
                                Shape {
                                    channels: config.channels,
                                    frames: 480,
                                },
                                48000,
                                config.frame_rate,
                            )
                            .with_context(|| "failed to create 2nd wrapping resampler")?,
                        ),
                    ]
                }
            }
        };

        log::info!("CrasProcessor created with: {:?}", config);
        Ok(CrasProcessor {
            check_shape,
            pipeline,
            _config: config,
        })
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
            })
            .expect("CrasProcessor::new with CrasProcessorEffect::NoEffects should never fail")
        }
    };

    *ret = export_plugin(processor);
    return success;
}
