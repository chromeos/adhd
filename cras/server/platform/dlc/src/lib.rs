// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

pub mod bindings;
#[cfg(feature = "dlc")]
mod chromiumos;
mod stub;
use std::collections::HashMap;
use std::sync::mpsc;
use std::sync::Mutex;
use std::time::Duration;
use std::time::Instant;
use std::time::{self};

use cras_common::string::null_terminated_char_array_from_str;
use cras_s2::global::cras_s2_get_dlcs_to_install;
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

#[derive(Error, Debug, Clone, Copy, PartialEq, Eq)]
pub enum Interrupt {
    #[error("restart loop")]
    Restart,
    #[error("shutdown loop")]
    Shutdown,
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

#[cfg(all(feature = "dlc", not(test)))]
type Service = chromiumos::Service;
#[cfg(any(not(feature = "dlc"), test))]
type Service = stub::Service;

static INTERRUPT_CHANNEL: Lazy<(
    mpsc::SyncSender<Interrupt>,
    Mutex<mpsc::Receiver<Interrupt>>,
)> = Lazy::new(|| {
    let (tx, rx) = mpsc::sync_channel(1);
    (tx, Mutex::new(rx))
});

pub fn trigger_task_interrupt(interrupt: Interrupt) {
    let (tx, rx_mutex) = &*INTERRUPT_CHANNEL;
    match interrupt {
        Interrupt::Shutdown => {
            // Drain the channel to ensure Shutdown can be pushed without blocking
            if let Ok(rx) = rx_mutex.lock() {
                while rx.try_recv().is_ok() {}
            }
            let _ = tx.try_send(interrupt);
        }
        Interrupt::Restart => {
            let _ = tx.try_send(interrupt);
        }
    }
}

pub fn trigger_recalculate() {
    trigger_task_interrupt(Interrupt::Restart);
}

pub fn shutdown_for_testing() {
    trigger_task_interrupt(Interrupt::Shutdown);
}

fn check_task_interrupt() -> Option<Interrupt> {
    let (_, rx) = &*INTERRUPT_CHANNEL;
    let rx = rx.lock().unwrap();
    match rx.try_recv() {
        Ok(interrupt) => Some(interrupt),
        Err(_) => None,
    }
}

fn wait_for_task_interrupt() -> Interrupt {
    let (_, rx) = &*INTERRUPT_CHANNEL;
    let rx = rx.lock().unwrap();
    match rx.recv() {
        Ok(interrupt) => interrupt,
        Err(_) => Interrupt::Shutdown,
    }
}

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

#[cfg(test)]
fn reset_state_cache() {
    STATE_CACHE.lock().unwrap().clear();
}

#[cfg(test)]
fn reset_interrupt_channel() {
    let (_, rx) = &*INTERRUPT_CHANNEL;
    let rx = rx.lock().unwrap();
    while rx.try_recv().is_ok() {}
}

fn handle_signals(
    metric_context: &DlcMetricContext,
    service: &mut Service,
    dlcs: &[String],
    timeout: Duration,
) -> Result<(), Interrupt> {
    let deadline = Instant::now() + timeout;
    while let Some(remaining_duration) = deadline.checked_duration_since(Instant::now()) {
        if let Some(interrupt) = check_task_interrupt() {
            return Err(interrupt);
        }
        match service.handle_one_signal(remaining_duration.min(Duration::from_millis(5000))) {
            Some((id, state)) => {
                if dlcs.contains(&id) {
                    set_dlc_state(metric_context, id, state, "handle_one_signal()");
                }
            }
            None => {}
        }
    }
    Ok(())
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
        Self {
            id: null_terminated_char_array_from_str(value),
        }
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

/// Performs a single pass of downloading and retrying all pending DLCs.
/// Returns `Result<(), Interrupt>` representing the outcome.
pub fn download_dlcs_until_installed(
    success_callback: DlcInstallOnSuccessCallback,
    failure_callback: DlcInstallOnFailureCallback,
) -> Result<(), Interrupt> {
    let mut service = Service::new().unwrap();

    let metric_context = DlcMetricContext {
        start_time: Instant::now(),
        success_callback,
        failure_callback,
    };

    // Consume any stale recalculate signal before beginning, propagating shutdown
    if let Some(Interrupt::Shutdown) = check_task_interrupt() {
        return Err(Interrupt::Shutdown);
    }

    let mut retry_sleep = time::Duration::from_secs(1);
    let max_retry_sleep = time::Duration::from_secs(120);
    let mut todo = cras_s2_get_dlcs_to_install(true);

    for _retry_count in 0..i32::MAX {
        if let Some(interrupt) = check_task_interrupt() {
            return Err(interrupt);
        }

        let mut todo_next = vec![];
        for dlc in todo.iter() {
            if let Some(interrupt) = check_task_interrupt() {
                return Err(interrupt);
            }

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
            return Ok(()); // Completed successfully
        }
        // The former dlc install could block the latter ones.
        // If the former dlc is broken, all the latter ones could be blocked.
        // We tried to avoid this issue by changing the order.
        todo_next.rotate_left(1);
        todo = todo_next;
        log::info!("retrying DLC installation in {retry_sleep:?}, order {todo:?}");
        handle_signals(&metric_context, &mut service, &todo, retry_sleep)?;
        retry_sleep = max_retry_sleep.min(retry_sleep * 2);
    }
    Ok(())
}

pub fn download_dlcs_until_installed_and_wait_for_recalculate(
    dlc_install_on_success_callback: DlcInstallOnSuccessCallback,
    dlc_install_on_failure_callback: DlcInstallOnFailureCallback,
) {
    loop {
        let result = download_dlcs_until_installed(
            dlc_install_on_success_callback,
            dlc_install_on_failure_callback,
        );

        match result {
            Err(Interrupt::Restart) => continue,
            Err(Interrupt::Shutdown) => break,
            Ok(()) => {}
        }

        // Fall-through wait state when all DLCs are successfully installed.
        match wait_for_task_interrupt() {
            Interrupt::Restart => continue,
            Interrupt::Shutdown => break,
        }
    }
}

#[cfg(test)]
mod tests {
    use std::thread;

    use super::*;

    static TEST_LOCK: std::sync::Mutex<()> = std::sync::Mutex::new(());

    extern "C" fn success_cb(_id: CrasDlcId128, _t: i32) -> libc::c_int {
        0
    }
    extern "C" fn failure_cb(_id: CrasDlcId128, _t: i32) -> libc::c_int {
        0
    }

    #[test]
    fn test_recalculate_aborts_and_restarts() {
        let _lock = TEST_LOCK.lock().unwrap();
        reset_overrides();
        reset_state_cache();
        reset_interrupt_channel();

        // Hook S2 globally to return dynamic values for tests if needed.
        // For this test, we'll rely on the loop aborting immediately when trigger_recalculate is called.
        let handle = thread::spawn(|| {
            download_dlcs_until_installed_and_wait_for_recalculate(success_cb, failure_cb);
        });

        // Give the thread some time to start and block inside the wait
        thread::sleep(Duration::from_millis(100));

        // Trigger the recalculation (Restart). The thread should receive it and restart/re-evaluate.
        trigger_task_interrupt(Interrupt::Restart);

        // Give it some time to process the restart and go back to waiting
        thread::sleep(Duration::from_millis(100));

        // Shutdown cleanly to exit the loop and avoid thread leak
        shutdown_for_testing();
        let _ = handle.join();

        reset_overrides();
        reset_state_cache();
        reset_interrupt_channel();
    }
}
