// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
use std::fs::File;
use std::io::{prelude::*, BufReader, BufWriter};
use std::path::PathBuf;

use serde::{Deserialize, Serialize};
use sys_util::info;

use crate::error::{Error, Result};

const DATASTORE_DIR: &str = "/var/lib/sound_card_init";

/// `Datastore`, which stores and reads calibration values in yaml format.
#[derive(Debug, Deserialize, Serialize, Copy, Clone)]
pub enum Datastore {
    /// Indicates using values in VPD.
    UseVPD,
    /// rdc is (11 / 3) / actual_rdc * 2^20.
    /// ambient_temp is actual_temp * 2^12 / 100.
    DSM { rdc: i32, ambient_temp: i32 },
}

impl Datastore {
    /// Creates a `Datastore` and initializes its fields from DATASTORE_DIR/<snd_card>/{file}.
    pub fn from_file(snd_card: &str, file: &str) -> Result<Datastore> {
        let path = PathBuf::from(DATASTORE_DIR).join(snd_card).join(file);

        let io_err = |e| Error::FileIOFailed(path.to_string_lossy().to_string(), e);
        let parse_err = |e: serde_yaml::Error| {
            Error::DeserializationFailed(path.to_string_lossy().to_string(), e)
        };

        let reader = BufReader::new(File::open(&path).map_err(io_err)?);
        let datastore: Datastore = serde_yaml::from_reader(reader).map_err(parse_err)?;
        Ok(datastore)
    }

    /// Saves a `Datastore` to DATASTORE_DIR/<snd_card>/{file}.
    pub fn save(&self, snd_card: &str, file: &str) -> Result<()> {
        let path = PathBuf::from(DATASTORE_DIR).join(snd_card).join(file);
        let io_err = |e| Error::FileIOFailed(path.to_string_lossy().to_string(), e);

        let mut writer = BufWriter::new(File::create(&path).map_err(io_err)?);
        writer
            .write(
                serde_yaml::to_string(self)
                    .map_err(Error::SerializationFailed)?
                    .as_bytes(),
            )
            .map_err(io_err)?;
        writer.flush().map_err(io_err)?;
        info!("update Datastore {}: {:?}", path.to_string_lossy(), self);
        Ok(())
    }
}
