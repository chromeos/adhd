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
use audio_processor::AudioProcessor;
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
                match config {
                    CrasProcessorConfig {
                        frame_rate: 48000,
                        channels: 1,
                        ..
                    } => (),
                    _ => {
                        return Err(audio_processor::Error::InvalidShape {
                            want_channels: 1,
                            want_frames: 48000,
                            got_channels: config.channels,
                            got_frames: config.frame_rate,
                        })
                        .with_context(|| {
                            "Unexpected config for NoiseCancellation in CrasProcessor::new"
                        });
                    }
                };

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
                    config.block_size,
                    config.channels,
                    config.frame_rate,
                )
                .with_context(|| "DynamicPluginProcessor::new failed")?;

                vec![Box::new(processor)]
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
#[no_mangle]
pub unsafe extern "C" fn cras_processor_create(
    config: *const CrasProcessorConfig,
) -> *mut plugin_processor {
    let config = match config.as_ref() {
        Some(config) => config,
        None => return std::ptr::null_mut(),
    };

    let processor = match CrasProcessor::new(config.clone()) {
        Ok(processor) => processor,
        Err(err) => {
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
    export_plugin(processor)
}
