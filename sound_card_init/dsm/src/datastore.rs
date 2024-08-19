// Copyright 2020 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
use std::fs::File;
use std::io::prelude::*;
use std::io::BufReader;
use std::io::BufWriter;
use std::path::PathBuf;

use log::info;
use serde::Deserialize;
use serde::Serialize;

use crate::error::Error;
use crate::error::Result;

/// `Datastore`, which stores and reads calibration values in yaml format.
#[derive(Debug, Deserialize, Serialize, Copy, Clone)]
pub enum Datastore {
    /// Indicates using values in VPD.
    UseVPD,
    DSM {
        rdc: i32,
        temp: i32,
    },
}

impl Datastore {
    /// The dir of datastore.
    pub const DATASTORE_DIR: &'static str = "/var/lib/sound_card_init";

    /// Creates a `Datastore` and initializes its fields from the datastore file.
    pub fn from_file(snd_card: &str, channel: usize) -> Result<Datastore> {
        let path = Self::path(snd_card, channel);
        let reader =
            BufReader::new(File::open(&path).map_err(|e| Error::FileIOFailed(path.to_owned(), e))?);
        let datastore: Datastore =
            serde_yaml::from_reader(reader).map_err(|e| Error::SerdeError(path.to_owned(), e))?;
        Ok(datastore)
    }

    /// Checks if the datastore file exists or not
    pub fn file_exists(snd_card: &str, channel: usize) -> Result<bool> {
        let path = Self::path(snd_card, channel);
        match File::open(&path).map_err(|e| Error::FileIOFailed(path.to_owned(), e)) {
            Ok(..) => return Ok(true),
            Err(e) => return Err(e),
        }
    }

    /// Saves a `Datastore` to file.
    pub fn save(&self, snd_card: &str, channel: usize) -> Result<()> {
        let path = Self::path(snd_card, channel);

        let mut writer = BufWriter::new(
            File::create(&path).map_err(|e| Error::FileIOFailed(path.to_owned(), e))?,
        );
        writer
            .write(
                serde_yaml::to_string(self)
                    .map_err(|e| Error::SerdeError(path.to_owned(), e))?
                    .as_bytes(),
            )
            .map_err(|e| Error::FileIOFailed(path.to_owned(), e))?;
        writer
            .flush()
            .map_err(|e| Error::FileIOFailed(path.to_owned(), e))?;
        info!("update Datastore {}: {:?}", path.to_string_lossy(), self);
        Ok(())
    }

    fn path(snd_card: &str, channel: usize) -> PathBuf {
        PathBuf::from(Self::DATASTORE_DIR)
            .join(snd_card)
            .join(format!("calib_{}", channel))
    }
}
