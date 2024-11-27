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
use std::time::Duration;
use std::time::Instant;
use std::time::{self};

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
    /// Handle a single signal from the service up to timeout.
    /// Optionally return the new state of a DLC.
    fn handle_one_signal(&mut self, timeout: Duration) -> Option<(String, State)>;
}

#[cfg(feature = "dlc")]
type Service = chromiumos::Service;
#[cfg(not(feature = "dlc"))]
type Service = stub::Service;

static STATE_OVERRIDES: Lazy<Mutex<HashMap<String, State>>> =
    Lazy::new(|| Mutex::new(Default::default()));

static STATE_CACHE: Lazy<Mutex<HashMap<String, State>>> =
    Lazy::new(|| Mutex::new(Default::default()));

fn install_dlc(service: &mut Service, id: &str) -> Result<State> {
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

fn handle_signals(
    metric_context: &DlcMetricContext,
    service: &mut Service,
    dlcs: &[String],
    timeout: Duration,
) {
    let deadline = Instant::now() + timeout;
    while let Some(remaining_duration) = deadline.checked_duration_since(Instant::now()) {
        match service.handle_one_signal(remaining_duration) {
            Some((id, state)) => {
                if dlcs.contains(&id) {
                    set_dlc_state(metric_context, id, state, "handle_one_signal()");
                }
            }
            None => {
                // handle_one_signal returned without giving us an update.
                // Sleep for the remaining duration.
                if let Some(remaining_duration) = deadline.checked_duration_since(Instant::now()) {
                    sleep(remaining_duration);
                    return;
                }
            }
        }
    }
}

fn set_dlc_state(metric_context: &DlcMetricContext, id: String, state: State, source: &str) {
    if state.installed {
        log::info!("successfully installed {id}; source: {source}");
        metric_context.record_success(&id);
    }
    cras_s2::global::cras_s2_set_dlc_installed(&id, state.installed);
    STATE_CACHE.lock().unwrap().insert(id, state);
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

struct DlcMetricContext {
    start_time: Instant,
    success_callback: DlcInstallOnSuccessCallback,
    failure_callback: DlcInstallOnFailureCallback,
}

impl DlcMetricContext {
    fn record_success(&self, id: &str) {
        (self.success_callback)(
            CrasDlcId128::from(id),
            self.start_time.elapsed().as_secs() as i32,
        );
    }

    fn record_failure(&self, id: &str) {
        (self.failure_callback)(
            CrasDlcId128::from(id),
            self.start_time.elapsed().as_secs() as i32,
        );
    }
}

fn download_dlcs_until_installed(
    dlc_install_on_success_callback: DlcInstallOnSuccessCallback,
    dlc_install_on_failure_callback: DlcInstallOnFailureCallback,
) {
    let mut service = Service::new().unwrap();

    let metric_context = DlcMetricContext {
        start_time: Instant::now(),
        success_callback: dlc_install_on_success_callback,
        failure_callback: dlc_install_on_failure_callback,
    };

    let mut retry_sleep = time::Duration::from_secs(1);
    let max_retry_sleep = time::Duration::from_secs(120);
    let mut todo = cras_s2_get_dlcs_to_install();
    for _retry_count in 0..i32::MAX {
        let mut todo_next = vec![];
        for dlc in todo.iter() {
            // The DLC is already registered to be installed according to D-Bus signals.
            // Skip this item.
            if STATE_CACHE
                .lock()
                .unwrap()
                .get(dlc)
                .is_some_and(|state| state.installed)
            {
                continue;
            }

            match install_dlc(&mut service, dlc) {
                Ok(state) => {
                    set_dlc_state(&metric_context, dlc.to_string(), state, "install_dlc()");
                }
                Err(e) => {
                    log::info!("failed to install {dlc}: {e}");
                    metric_context.record_failure(dlc);
                    todo_next.push(dlc.to_string());
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
        handle_signals(&metric_context, &mut service, &todo, retry_sleep);
        retry_sleep = max_retry_sleep.min(retry_sleep * 2);
    }
}
