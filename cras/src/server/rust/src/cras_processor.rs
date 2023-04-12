// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use audio_processor::processors::binding::plugin_processor;
use audio_processor::processors::export_plugin;
use audio_processor::processors::CheckShape;
use audio_processor::processors::DynamicPluginProcessor;
use audio_processor::processors::NegateAudioProcessor;
use audio_processor::AudioProcessor;

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
    plugin: Option<Box<dyn AudioProcessor<I = f32, O = f32>>>,
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
        if let Some(plugin) = &mut self.plugin {
            input = plugin.process(input)?
        }

        Ok(input)
    }
}

impl CrasProcessor {
    fn new(config: CrasProcessorConfig) -> Self {
        let effect = match config {
            CrasProcessorConfig {
                frame_rate: 48000,
                channels: 1,
                effect,
                ..
            } => effect,
            _ => {
                let err = audio_processor::Error::InvalidShape {
                    want_channels: 1,
                    want_frames: 48000,
                    got_channels: config.channels,
                    got_frames: config.frame_rate,
                };
                log::error!("Unexpected config in CrasProcessor::new: {:?}", err);
                CrasProcessorEffect::NoEffects
            }
        };

        let plugin: Option<Box<dyn AudioProcessor<I = f32, O = f32>>> = match effect {
            CrasProcessorEffect::NoEffects => None,
            CrasProcessorEffect::Negate => Some(Box::new(NegateAudioProcessor::new(
                config.channels,
                config.block_size,
            ))),
            CrasProcessorEffect::NoiseCancellation => match DynamicPluginProcessor::new(
                // TODO: Remove hard coded path once https://crrev.com/c/4344830 lands.
                "/run/imageloader/nc-ap-dlc/package/root/libdenoiser.so",
                "plugin_processor_create",
                config.block_size,
                config.channels,
                config.frame_rate,
            ) {
                Ok(plugin) => Some(Box::new(plugin)),
                Err(err) => {
                    log::error!("Cannot load plugin: {}", err);

                    // Cannot load plugin.
                    // Still proceed to create a no-op processor.
                    None
                }
            },
        };

        log::info!("CrasProcessor created with: {:?}", config);

        CrasProcessor {
            check_shape: CheckShape::new(config.channels, config.block_size),
            plugin,
            _config: config,
        }
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

    export_plugin(CrasProcessor::new(config.clone()))
}
