// Copyright 2020 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
use std::fs::File;
use std::io::prelude::*;
use std::io::BufReader;
use std::path::PathBuf;

use crate::error::Error;
use crate::error::Result;
use crate::Datastore;
use crate::VPDTrait;

const VPD_DIR: &str = "/sys/firmware/vpd/ro/vpdfile";

/// `VPD`, which represents the amplifier factory calibration values.
#[derive(Default, Debug)]
pub struct VPD {
    pub dsm_calib_r0: i32,
    pub dsm_calib_temp: i32,
}

impl VPDTrait for VPD {
    /// Creates a `VPD` and initializes its fields from VPD_DIR/dsm_calib_r0_{channel}.
    /// # Arguments
    ///
    /// * `channel` - channel number.
    fn new(channel: usize) -> Result<VPD> {
        let mut vpd: VPD = Default::default();
        vpd.dsm_calib_r0 = read_vpd_files(&format!("dsm_calib_r0_{}", channel))?;
        vpd.dsm_calib_temp = read_vpd_files(&format!("dsm_calib_temp_{}", channel))?;
        Ok(vpd)
    }
    fn new_from_datastore(datastore: Datastore, channel: usize) -> Result<VPD> {
        match datastore {
            Datastore::UseVPD => VPD::new(channel),
            Datastore::DSM { rdc, temp } => {
                let mut vpd: VPD = Default::default();
                vpd.dsm_calib_r0 = rdc;
                vpd.dsm_calib_temp = temp as i32;
                Ok(vpd)
            }
        }
    }
}

fn read_vpd_files(file: &str) -> Result<i32> {
    let path = PathBuf::from(VPD_DIR).with_file_name(file);
    let io_err = |e| Error::FileIOFailed(path.to_owned(), e);
    let mut reader = BufReader::new(File::open(&path).map_err(io_err)?);
    let mut line = String::new();
    reader.read_line(&mut line).map_err(io_err)?;
    line.parse::<i32>()
        .map_err(|e| Error::VPDParseFailed(path.to_string_lossy().to_string(), e))
}
