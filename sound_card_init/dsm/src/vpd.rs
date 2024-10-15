// Copyright 2020 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
use std::fs::File;
use std::io::prelude::*;
use std::io::BufReader;
use std::path::PathBuf;
use std::process::Command;

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

/// `Tas2563VPD`, which represents the amplifier factory calibration values for
/// TAS2563.
/// A separate struct is used, as TAS2563 stores different values.
#[derive(Default, Debug)]
pub struct Tas2563VPD {
    // The calibrated value of each channel has 21 bytes in total.
    pub dsm_calib_value: Vec<u8>,
    // The array of id and register addresses have 16 bytes in total.
    pub dsm_calib_register_array: Vec<u8>,
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

impl VPDTrait for Tas2563VPD {
    /// Creates a `Tas2563VPD` and initializes its fields from VPD_DIR/dsm_calib_value_{channel}.
    /// # Arguments
    ///
    /// * `channel` - channel number.
    fn new(channel: usize) -> Result<Tas2563VPD> {
        let mut vpd: Tas2563VPD = Default::default();
        vpd.dsm_calib_value = read_vpd_files_hex_string(&format!("dsm_calib_value_{}", channel))?;
        vpd.dsm_calib_register_array =
            match read_vpd_files_hex_string(&format!("dsm_calib_register_array")) {
                Ok(array) => array,
                // If the register_array is not given, use a set of fixed values.
                Err(e) => vec![
                    0x72, 0x00, 0x0f, 0x34, 0x00, 0x0f, 0x48, 0x00, 0x0f, 0x40, 0x00, 0x0d, 0x3c,
                    0x00, 0x10, 0x14,
                ],
            };
        Ok(vpd)
    }
    fn new_from_datastore(datastore: Datastore, channel: usize) -> Result<Tas2563VPD> {
        match datastore {
            Datastore::UseVPD => Tas2563VPD::new(channel),
            Datastore::DSM { rdc: _, temp: _ } => Err(Error::InvalidDatastore),
        }
    }
}

pub fn update_vpd(channel: usize, vpd: VPD) -> Result<()> {
    write_to_vpd(&format!("dsm_calib_r0_{}", channel), vpd.dsm_calib_r0)?;
    write_to_vpd(&format!("dsm_calib_temp_{}", channel), vpd.dsm_calib_temp)?;
    Ok(())
}

fn write_to_vpd(key: &str, value: i32) -> Result<()> {
    let _output = Command::new("vpd")
        .arg("-s")
        .arg(&format!("{}={}", key, value))
        .output()?;
    Ok(())
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

fn read_vpd_files_hex_string(file: &str) -> Result<Vec<u8>> {
    let path = PathBuf::from(VPD_DIR).with_file_name(file);
    let io_err = |e| Error::FileIOFailed(path.to_owned(), e);
    let mut reader = BufReader::new(File::open(&path).map_err(io_err)?);
    let mut line = String::new();
    reader.read_line(&mut line).map_err(io_err)?;
    // Convert hex string to bytes
    if line.len() % 2 != 0 {
        return Err(Error::VPDParseHexStringFailed(
            path.to_string_lossy().to_string(),
            line.clone(),
            line.len(),
        ));
    }
    let mut vec = Vec::new();
    for i in (0..line.len()).step_by(2) {
        vec.push(u8::from_str_radix(&line[i..i + 2], 16).unwrap())
    }
    Ok(vec)
}
