// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use std::sync::mpsc::channel;
use std::sync::mpsc::Receiver;
use std::sync::mpsc::TryRecvError;
use std::time::Duration;

use dbus::blocking::Connection;
use dbus::blocking::Proxy;
use dbus::message::MatchRule;
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
pub struct Service {
    connection: Connection,
    state_change_rx: Receiver<Vec<u8>>,
}

impl super::ServiceTrait for Service {
    fn new() -> Result<Self> {
        let connection = Connection::new_system()?;

        let (tx, state_change_rx) = channel();

        let mr = MatchRule::new_signal("org.chromium.DlcServiceInterface", "DlcStateChanged");
        connection.add_match(mr, move |(raw_bytes,): (Vec<u8>,), _, _| {
            if let Err(err) = tx.send(raw_bytes) {
                log::error!("cannot send DLC state change {err:#}");
            }

            true // Keep the matcher alive.
        })?;

        Ok(Self {
            connection,
            state_change_rx,
        })
    }

    fn install(&mut self, id: &str) -> Result<()> {
        let conn_path = get_dlcservice_connection_path(&self.connection);

        let mut request = InstallRequest::new();
        request.id = id.to_string();
        request.reserve = true;
        Ok(conn_path.install(request.write_to_bytes()?)?)
    }

    fn get_dlc_state(&mut self, id: &str) -> Result<crate::State> {
        let conn_path = get_dlcservice_connection_path(&self.connection);

        let res = conn_path.get_dlc_state(id)?;
        let mut dlc_state = DlcState::new();
        dlc_state.merge_from_bytes(&res)?;
        Ok(crate::State::from(dlc_state))
    }

    fn handle_one_signal(
        &mut self,
        timeout: std::time::Duration,
    ) -> Option<(String, crate::State)> {
        if let Err(err) = self.connection.process(timeout) {
            log::error!("self.connection.process failed: {err:#}");
            return None;
        }

        match self.state_change_rx.try_recv() {
            Ok(raw_bytes) => {
                let mut dlc_state = DlcState::new();
                match dlc_state.merge_from_bytes(&raw_bytes) {
                    Ok(state) => Some((dlc_state.id.clone(), crate::State::from(dlc_state))),
                    Err(err) => {
                        log::error!("cannot parse DLC state: {err:#}");
                        None
                    }
                }
            }
            Err(TryRecvError::Empty) => None,
            Err(TryRecvError::Disconnected) => {
                log::error!("channel sender is disconnected. This is a bug!");
                None
            }
        }
    }
}

impl From<DlcState> for crate::State {
    fn from(dlc_state: DlcState) -> Self {
        Self {
            installed: dlc_state.state.enum_value() == Ok(State::INSTALLED),
            root_path: dlc_state.root_path,
        }
    }
}
