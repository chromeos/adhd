// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::cras_features_bindings;
use crate::decls;
use crate::Backend;

pub struct BackendStub;

impl Backend for BackendStub {
    fn new(
        _changed_callback: cras_features_bindings::cras_features_notify_changed,
    ) -> anyhow::Result<Self> {
        Ok(Self)
    }

    fn is_enabled(&self, id: usize) -> bool {
        eprintln!(
            "{id} {:?}",
            decls::FEATURES.get(id).map(|f| f.default_enabled)
        );
        decls::FEATURES
            .get(id)
            .map(|f| f.default_enabled)
            .unwrap_or(false)
    }
}
