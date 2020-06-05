// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
use std::fs::File;
use std::io::prelude::*;
use std::io::BufReader;
use std::path::PathBuf;

use crate::error::{Error, Result};

const VPD_DIR: &str = "/sys/firmware/vpd/ro/vpdfile";

/// `VPD`, which represents the amplifier factory calibration values.
#[derive(Default, Debug)]
pub struct VPD {
    /// dsm_calib_r0 is (11 / 3) / actual_rdc * 2^20.
    pub dsm_calib_r0: i32,
    /// dsm_calib_temp is actual_temp * 2^12 / 100.
    pub dsm_calib_temp: i32,
}

impl VPD {
    /// Creates a `VPD` and initializes its fields from the given VPD files.
    pub fn from_file(rdc_file: &str, temp_file: &str) -> Result<VPD> {
        let mut vpd: VPD = Default::default();
        vpd.dsm_calib_r0 = read_vpd_files(rdc_file)?;
        vpd.dsm_calib_temp = read_vpd_files(temp_file)?;
        Ok(vpd)
    }
}

fn read_vpd_files(file: &str) -> Result<i32> {
    let path = PathBuf::from(VPD_DIR).with_file_name(file);
    let io_err = |e| Error::FileIOFailed(path.to_string_lossy().to_string(), e);
    let mut reader = BufReader::new(File::open(&path).map_err(io_err)?);
    let mut line = String::new();
    reader.read_line(&mut line).map_err(io_err)?;
    line.parse::<i32>()
        .map_err(|e| Error::VPDParseFailed(path.to_string_lossy().to_string(), e))
}
