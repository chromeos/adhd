// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use std::time::Duration;

use dbus::blocking::Connection;
use dbus::blocking::Proxy;
use protobuf::Message;
use system_api::client::OrgChromiumDlcServiceInterface;
use system_api::dlcservice::dlc_state::State;
use system_api::dlcservice::DlcState;
use system_api::dlcservice::InstallRequest;

use super::Result;

const DBUS_TIMEOUT: Duration = Duration::from_secs(3);

fn get_dlcservice_connection_path(connection: &Connection) -> Proxy<&Connection> {
    connection.with_proxy(
        "org.chromium.DlcService",
        "/org/chromium/DlcService",
        DBUS_TIMEOUT,
    )
}

/// Provides a DLC service implementation backed by communicating
/// with the DLC service through D-Bus.
pub struct Service;

impl super::ServiceTrait for Service {
    fn new() -> Result<Self> {
        Ok(Self)
    }

    fn install(&mut self, id: &str) -> Result<()> {
        let connection = Connection::new_system()?;
        let conn_path = get_dlcservice_connection_path(&connection);

        let mut request = InstallRequest::new();
        request.id = id.to_string();
        request.reserve = true;
        Ok(conn_path.install(request.write_to_bytes()?)?)
    }

    fn get_dlc_state(&mut self, id: &str) -> Result<crate::State> {
        let connection = Connection::new_system()?;
        let conn_path = get_dlcservice_connection_path(&connection);

        let res = conn_path.get_dlc_state(id)?;
        let mut dlc_state = DlcState::new();
        dlc_state.merge_from_bytes(&res)?;
        Ok(crate::State {
            installed: dlc_state.state.enum_value() == Ok(State::INSTALLED),
            root_path: dlc_state.root_path,
        })
    }
}
