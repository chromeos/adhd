// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

pub mod bindings;
#[cfg(feature = "dlc")]
mod chromiumos;
mod stub;

use std::fmt::Display;

use thiserror::Error;

pub struct State {
    pub installed: bool,
    pub root_path: String,
}

#[derive(Error, Debug)]
pub enum Error {
    #[error("D-Bus failure: {0:#}")]
    DBus(#[from] dbus::Error),
    #[error("protocol buffers failure: {0:#}")]
    Protobuf(#[from] protobuf::Error),
    #[error("CString failure: {0:#}")]
    CString(#[from] std::ffi::NulError),
    #[error("unknown DLC state: {0}")]
    UnknownDlcState(i32),
    #[error("unsupported operation")]
    Unsupported,
}

pub type Result<T, E = Error> = std::result::Result<T, E>;

/// All supported DLCs in CRAS.
#[repr(C)]
#[derive(Clone, Copy)]
pub enum CrasDlcId {
    CrasDlcSrBt,
    CrasDlcNcAp,
    NumCrasDlc,
}

impl CrasDlcId {
    fn as_str(&self) -> &'static str {
        match self {
            CrasDlcId::CrasDlcSrBt => "sr-bt-dlc",
            CrasDlcId::CrasDlcNcAp => "nc-ap-dlc",
            CrasDlcId::NumCrasDlc => "num",
        }
    }
}

impl Display for CrasDlcId {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        f.write_str(self.as_str())
    }
}

trait ServiceTrait: Sized {
    fn new() -> Result<Self>;
    fn install(&mut self, id: CrasDlcId) -> Result<()>;
    fn get_dlc_state(&mut self, id: CrasDlcId) -> Result<State>;
}

#[cfg(feature = "dlc")]
type Service = chromiumos::Service;
#[cfg(not(feature = "dlc"))]
type Service = stub::Service;

pub fn install_dlc(id: CrasDlcId) -> Result<()> {
    let mut service = Service::new()?;
    service.install(id)
}

pub fn get_dlc_state(id: CrasDlcId) -> Result<State> {
    let mut service = Service::new()?;
    service.get_dlc_state(id)
}
