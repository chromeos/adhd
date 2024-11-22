// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use super::Result;

/// Provides a stub DLC service implementation which always fails to install.
pub(super) struct Service;

impl super::ServiceTrait for Service {
    fn new() -> Result<Self> {
        Ok(Self)
    }

    fn install(&mut self, _id: &str) -> Result<()> {
        Err(super::Error::Unsupported)
    }

    fn get_dlc_state(&mut self, _id: &str) -> Result<super::State> {
        Ok(super::State {
            installed: false,
            root_path: String::new(),
        })
    }
}
