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
use std::thread::sleep;
use std::time;

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
    #[error("DLC not installed after install call")]
    NotInstalledAfterInstallCall,
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
}

// The list of DLCs that are installed automatically.
const MANAGED_DLCS: &[CrasDlcId] = &[CrasDlcId::CrasDlcSrBt, CrasDlcId::CrasDlcNcAp];

pub const CRAS_DLC_ID_STRING_MAX_LENGTH: i32 = 50;
impl CrasDlcId {
    fn as_str(&self) -> &'static str {
        match self {
            // The length of these strings should be bounded by
            // CRAS_DLC_ID_STRING_MAX_LENGTH
            CrasDlcId::CrasDlcSrBt => "sr-bt-dlc",
            CrasDlcId::CrasDlcNcAp => "nc-ap-dlc",
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

static STATE_CACHE: Lazy<Mutex<HashMap<CrasDlcId, State>>> =
    Lazy::new(|| Mutex::new(Default::default()));

pub fn install_dlc(id: CrasDlcId) -> Result<State> {
    let mut service = Service::new()?;
    service.install(id)?;
    let state = service.get_dlc_state(id)?;
    if !state.installed {
        return Err(Error::NotInstalledAfterInstallCall);
    }
    Ok(state)
}

pub fn get_dlc_state_cached(id: CrasDlcId) -> State {
    // Return override if exist.
    if let Some(state) = STATE_OVERRIDES.lock().unwrap().get(&id) {
        return state.clone();
    }

    STATE_CACHE
        .lock()
        .unwrap()
        .get(&id)
        .cloned()
        .unwrap_or_else(|| State {
            installed: false,
            root_path: String::new(),
        })
}

fn override_state_for_testing(id: CrasDlcId, state: State) {
    STATE_OVERRIDES.lock().unwrap().insert(id, state);
}

fn reset_overrides() {
    STATE_OVERRIDES.lock().unwrap().clear();
}

type CrasServerMetricsDlcInstallRetriedTimesOnSuccessFunc =
    extern "C" fn(CrasDlcId, i32) -> libc::c_int;

fn download_dlcs_until_installed(
    cras_server_metrics_dlc_install_retried_times_on_success: CrasServerMetricsDlcInstallRetriedTimesOnSuccessFunc,
) {
    let mut retry_sleep = time::Duration::from_secs(30);
    let max_retry_sleep = time::Duration::from_secs(1800);
    let mut todo: Vec<_> = MANAGED_DLCS.iter().collect();
    for retry_count in 0..i32::MAX {
        let mut todo_next = vec![];
        for &dlc in todo.iter() {
            match install_dlc(*dlc) {
                Ok(state) => {
                    log::info!("successfully installed {dlc}");
                    STATE_CACHE.lock().unwrap().insert(*dlc, state);
                    cras_server_metrics_dlc_install_retried_times_on_success(*dlc, retry_count);
                }
                Err(e) => {
                    log::info!("failed to install {dlc}: {e}");
                    todo_next.push(dlc);
                }
            }
        }

        cras_s2::global::set_dlc_manager_ready();

        if todo_next.is_empty() {
            return;
        }
        log::info!("retrying DLC installation in {retry_sleep:?}");
        sleep(retry_sleep);
        retry_sleep = max_retry_sleep.min(retry_sleep * 2);
        todo = todo_next;
    }
}
