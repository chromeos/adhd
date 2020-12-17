// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//! `amp` crate provides `Amp` trait for amplifier initializations and `AmpBuilder`
//! to create `Amp` objects.
#![deny(missing_docs)]

mod max98390d;

use dsm::Error;

use max98390d::Max98390;

type Result<T> = std::result::Result<T, Error>;
const CONF_DIR: &str = "/etc/sound_card_init";

/// It creates `Amp` object based on the sound card name.
pub struct AmpBuilder<'a> {
    sound_card_id: &'a str,
}

impl<'a> AmpBuilder<'a> {
    /// Creates an `AmpBuilder`.
    pub fn new(sound_card_id: &'a str) -> Self {
        AmpBuilder { sound_card_id }
    }

    /// Creates an `Amp` based on the sound card name.
    pub fn build(&self) -> Result<Box<dyn Amp>> {
        match self.sound_card_id {
            "sofcmlmax98390d" => Ok(Box::new(Max98390::new(self.sound_card_id)?) as Box<dyn Amp>),
            _ => Err(Error::UnsupportedSoundCard(self.sound_card_id.to_owned())),
        }
    }
}

/// It defines the required functions of amplifier objects.
pub trait Amp {
    /// The amplifier boot time calibration flow.
    fn boot_time_calibration(&mut self) -> Result<()>;
}
