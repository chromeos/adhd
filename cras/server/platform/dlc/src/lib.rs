// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

pub mod bindings;
#[cfg(feature = "dlc")]
mod chromiumos;
mod stub;

use std::collections::HashMap;
use std::sync::Mutex;
use std::thread::sleep;
use std::time;

use cras_common::types_internal::CrasDlcId;
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

// Called when a dlc is installed successfully, with the following arguments:
// - CrasDlcId: the id of the installed dlc.
// - i32: the number of retried times.
type DlcInstallOnSuccessCallback = extern "C" fn(CrasDlcId, i32) -> libc::c_int;

#[repr(C)]
pub struct CrasDlcDownloadConfig {
    pub dlcs_to_download: [bool; cras_common::types_internal::NUM_CRAS_DLCS],
}

fn download_dlcs_until_installed(
    download_config: CrasDlcDownloadConfig,
    dlc_install_on_success_callback: DlcInstallOnSuccessCallback,
) {
    let mut retry_sleep = time::Duration::from_secs(1);
    let max_retry_sleep = time::Duration::from_secs(120);
    let mut todo: Vec<_> = download_config
        .dlcs_to_download
        .iter()
        .zip(cras_common::types_internal::MANAGED_DLCS.iter())
        .filter_map(|(download, id)| if *download { Some(id) } else { None })
        .collect();
    for retry_count in 0..i32::MAX {
        let mut todo_next = vec![];
        for &dlc in todo.iter() {
            match install_dlc(*dlc) {
                Ok(state) => {
                    log::info!("successfully installed {dlc}");
                    STATE_CACHE.lock().unwrap().insert(*dlc, state);
                    cras_s2::global::cras_s2_set_dlc_installed(*dlc);
                    dlc_install_on_success_callback(*dlc, retry_count);
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
        // The former dlc install could block the latter ones.
        // If the former dlc is broken, all the latter ones could be blocked.
        // We tried to avoid this issue by changing the order.
        todo_next.rotate_left(1);
        todo = todo_next;
        log::info!("retrying DLC installation in {retry_sleep:?}, order {todo:?}");
        sleep(retry_sleep);
        retry_sleep = max_retry_sleep.min(retry_sleep * 2);
    }
    cras_s2::global::set_dlc_manager_done();
}
