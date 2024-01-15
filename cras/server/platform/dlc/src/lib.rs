// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

pub mod bindings;
#[cfg(feature = "dlc")]
mod chromiumos;
mod stub;

use std::collections::HashMap;
use std::fmt::Display;
use std::sync::Mutex;

use once_cell::sync::Lazy;
use thiserror::Error;

#[derive(Clone)]
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
#[derive(Clone, Copy, PartialEq, Hash, Eq)]
pub enum CrasDlcId {
    CrasDlcSrBt,
    CrasDlcNcAp,
    NumCrasDlc,
}

pub const CRAS_DLC_ID_STRING_MAX_LENGTH: i32 = 50;
impl CrasDlcId {
    fn as_str(&self) -> &'static str {
        match self {
            // The length of these strings should be bounded by
            // CRAS_DLC_ID_STRING_MAX_LENGTH
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

static STATE_OVERRIDES: Lazy<Mutex<HashMap<CrasDlcId, State>>> =
    Lazy::new(|| Mutex::new(Default::default()));

pub fn install_dlc(id: CrasDlcId) -> Result<()> {
    let mut service = Service::new()?;
    service.install(id)
}

pub fn get_dlc_state(id: CrasDlcId) -> Result<State> {
    // Return override if exist.
    if let Some(state) = STATE_OVERRIDES.lock().unwrap().get(&id) {
        return Ok(state.clone());
    }

    let mut service = Service::new()?;
    service.get_dlc_state(id)
}

fn override_state_for_testing(id: CrasDlcId, state: State) {
    STATE_OVERRIDES.lock().unwrap().insert(id, state);
}

fn reset_overrides() {
    STATE_OVERRIDES.lock().unwrap().clear();
}
