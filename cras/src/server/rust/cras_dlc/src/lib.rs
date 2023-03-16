// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use std::{ffi::CString, os::raw::c_char, ptr, time::Duration};

use {dbus::blocking::Connection, protobuf::Message, thiserror::Error};

use system_api::{
    client::OrgChromiumDlcServiceInterface,
    dlcservice::{DlcState, DlcState_State, InstallRequest},
};

const DBUS_TIMEOUT: Duration = Duration::from_secs(3);

const DLC_ID_NC_AP: &str = "nc-ap-dlc";

#[derive(Error, Debug)]
enum Error {
    #[error("D-Bus failure: {0:#}")]
    DBusError(#[from] dbus::Error),
    #[error("protocol buffers failure: {0:#}")]
    ProtobufError(#[from] protobuf::ProtobufError),
    #[error("CString failure: {0:#}")]
    CStringError(#[from] std::ffi::NulError),
}

type Result<T> = anyhow::Result<T, Error>;

fn install_and_get_dlc_state(id: &str) -> Result<DlcState> {
    let connection = Connection::new_system()?;
    let conn_path = connection.with_proxy(
        "org.chromium.DlcService",
        "/org/chromium/DlcService",
        DBUS_TIMEOUT,
    );
    let mut request = InstallRequest::new();
    request.set_id(id.to_string());
    request.set_reserve(true);
    conn_path.install(request.write_to_bytes()?)?;

    let res = conn_path.get_dlc_state(id)?;
    let mut dlc_state = DlcState::new();
    dlc_state.merge_from_bytes(&res)?;
    Ok(dlc_state)
}

fn sr_bt_is_available() -> Result<DlcState_State> {
    let dlc_state = install_and_get_dlc_state("sr-bt-dlc")?;
    Ok(dlc_state.state)
}

fn sr_bt_get_root() -> Result<CString> {
    let dlc_state = install_and_get_dlc_state("sr-bt-dlc")?;
    CString::new(dlc_state.root_path).map_err(|e| e.into())
}

fn nc_ap_is_available() -> Result<DlcState_State> {
    let dlc_state = install_and_get_dlc_state(DLC_ID_NC_AP)?;
    Ok(dlc_state.state)
}

fn nc_ap_get_root() -> Result<CString> {
    let dlc_state = install_and_get_dlc_state(DLC_ID_NC_AP)?;
    CString::new(dlc_state.root_path).map_err(|e| e.into())
}

/// Returns `true` if the "sr-bt-dlc" packge is ready for use, otherwise
/// retuns `false`.
#[no_mangle]
pub unsafe extern "C" fn cras_dlc_sr_bt_is_available() -> bool {
    match sr_bt_is_available() {
        Ok(state) => state == DlcState_State::INSTALLED,
        Err(_) => false,
    }
}

/// Returns Dlc root_path for the "sr-bt-dlc" package.
#[no_mangle]
pub unsafe extern "C" fn cras_dlc_sr_bt_get_root() -> *const c_char {
    match sr_bt_get_root() {
        Ok(root_path) => root_path.into_raw(),
        Err(_) => ptr::null_mut(),
    }
}

/// Returns `true` if the "nc-ap-dlc" package is ready for use, otherwise
/// returns `false`.
#[no_mangle]
pub unsafe extern "C" fn cras_dlc_nc_ap_is_available() -> bool {
    match nc_ap_is_available() {
        Ok(state) => state == DlcState_State::INSTALLED,
        Err(_) => false,
    }
}

/// Returns DLC root_path for the "nc-ap-dlc" package.
#[no_mangle]
pub unsafe extern "C" fn cras_dlc_nc_ap_get_root() -> *const c_char {
    match nc_ap_get_root() {
        Ok(root_path) => root_path.into_raw(),
        Err(_) => ptr::null_mut(),
    }
}
