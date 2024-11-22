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

use cras_s2::global::cras_s2_get_dlcs_to_install;
use once_cell::sync::Lazy;
use thiserror::Error;
use zerocopy::AsBytes;

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
    fn install(&mut self, id: &str) -> Result<()>;
    fn get_dlc_state(&mut self, id: &str) -> Result<State>;
}

#[cfg(feature = "dlc")]
type Service = chromiumos::Service;
#[cfg(not(feature = "dlc"))]
type Service = stub::Service;

static STATE_OVERRIDES: Lazy<Mutex<HashMap<String, State>>> =
    Lazy::new(|| Mutex::new(Default::default()));

static STATE_CACHE: Lazy<Mutex<HashMap<String, State>>> =
    Lazy::new(|| Mutex::new(Default::default()));

pub fn install_dlc(id: &str) -> Result<State> {
    let mut service = Service::new()?;
    service.install(id)?;
    let state = service.get_dlc_state(id)?;
    if !state.installed {
        return Err(Error::NotInstalledAfterInstallCall);
    }
    Ok(state)
}

pub fn get_dlc_state_cached(id: &str) -> State {
    // Return override if exist.
    if let Some(state) = STATE_OVERRIDES.lock().unwrap().get(id) {
        return state.clone();
    }

    STATE_CACHE
        .lock()
        .unwrap()
        .get(id)
        .cloned()
        .unwrap_or_else(|| State {
            installed: false,
            root_path: String::new(),
        })
}

fn override_state_for_testing(id: &str, state: State) {
    STATE_OVERRIDES
        .lock()
        .unwrap()
        .insert(id.to_string(), state);
}

fn reset_overrides() {
    STATE_OVERRIDES.lock().unwrap().clear();
}

/// This type exists as an alternative to heap-allocated C-strings.
///
/// This type, as a simple value, is free of ownership or memory leak issues,
/// when we pass this in a callback we don't have to worry about who should free the string.
#[repr(C)]
pub struct CrasDlcId128 {
    id: [libc::c_char; 128],
}

impl From<&str> for CrasDlcId128 {
    fn from(value: &str) -> Self {
        let b = value.as_bytes();
        let to_copy = b.len().min(127);
        let mut id = [0; 128];
        id.as_bytes_mut()[..to_copy].clone_from_slice(&b[..to_copy]);
        Self { id }
    }
}

// Called when a dlc is installed successfully, with the following arguments:
// - id: the id of the installed dlc.
// - elapsed_seconds: the elapsed time when the installation succeeded.
type DlcInstallOnSuccessCallback =
    extern "C" fn(id: CrasDlcId128, elapsed_seconds: i32) -> libc::c_int;

// Called when a dlc failed to install, with the following arguments:
// - id: the id of the installed dlc.
// - elapsed_seconds: the elapsed time when the installation failed.
type DlcInstallOnFailureCallback =
    extern "C" fn(id: CrasDlcId128, elapsed_seconds: i32) -> libc::c_int;

fn download_dlcs_until_installed(
    dlc_install_on_success_callback: DlcInstallOnSuccessCallback,
    dlc_install_on_failure_callback: DlcInstallOnFailureCallback,
) {
    let start_time = time::Instant::now();

    let mut retry_sleep = time::Duration::from_secs(1);
    let max_retry_sleep = time::Duration::from_secs(120);
    let mut todo = cras_s2_get_dlcs_to_install();
    for _retry_count in 0..i32::MAX {
        let mut todo_next = vec![];
        for dlc in todo.iter() {
            match install_dlc(dlc) {
                Ok(state) => {
                    log::info!("successfully installed {dlc}");
                    STATE_CACHE.lock().unwrap().insert(dlc.to_string(), state);
                    cras_s2::global::cras_s2_set_dlc_installed(dlc, true);
                    dlc_install_on_success_callback(
                        CrasDlcId128::from(dlc.as_str()),
                        start_time.elapsed().as_secs() as i32,
                    );
                }
                Err(e) => {
                    log::info!("failed to install {dlc}: {e}");
                    todo_next.push(dlc.to_string());
                    dlc_install_on_failure_callback(
                        CrasDlcId128::from(dlc.as_str()),
                        start_time.elapsed().as_secs() as i32,
                    );
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
}
