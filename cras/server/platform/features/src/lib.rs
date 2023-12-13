// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use std::sync::RwLock;

use once_cell::sync::Lazy;

mod stub;

mod decls {
    include!(concat!(env!("OUT_DIR"), "/feature_decls.rs"));
}
mod cras_features_bindings {
    #![allow(non_upper_case_globals)]
    #![allow(non_camel_case_types)]
    #![allow(non_snake_case)]
    #![allow(unused)]
    include!(concat!(env!("OUT_DIR"), "/cras_features_bindings.rs"));
}

trait Backend: Sized {
    fn new(
        changed_callback: cras_features_bindings::cras_features_notify_changed,
    ) -> anyhow::Result<Self>;

    fn is_enabled(&self, id: usize) -> bool;
}

cfg_if::cfg_if! {
    if #[cfg(feature = "chromiumos")] {
        mod chromiumos;
        use chromiumos::BackendFeatured as BackendImpl;
    } else {
        use stub::BackendStub as BackendImpl;
    }
}

static GLOBAL_REGISTRY: Lazy<RwLock<Option<BackendImpl>>> = Lazy::new(|| RwLock::new(None));

/// See features_impl.h
#[no_mangle]
pub fn cras_features_backend_init(
    changed_callback: cras_features_bindings::cras_features_notify_changed,
) {
    match BackendImpl::new(changed_callback) {
        Ok(registry) => {
            *GLOBAL_REGISTRY.write().unwrap() = Some(registry);
        }
        Err(e) => {
            log::error!("cannot initialize feature registry: {e:?}");
        }
    }
}

/// See features_impl.h
#[no_mangle]
pub fn cras_features_backend_deinit() {
    *GLOBAL_REGISTRY.write().unwrap() = None;
}

/// See features_impl.h
#[no_mangle]
pub fn cras_features_backend_get_enabled(id: cras_features_bindings::cras_feature_id) -> bool {
    if id >= cras_features_bindings::cras_feature_id_NUM_FEATURES {
        return false;
    }
    let id = id as usize;
    match GLOBAL_REGISTRY.read().unwrap().as_ref() {
        Some(registry) => registry.is_enabled(id),
        None => decls::FEATURES[id].default_enabled,
    }
}
