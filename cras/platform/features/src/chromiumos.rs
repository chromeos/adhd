// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use std::sync::Arc;
use std::sync::Mutex;
use std::time::Duration;

use anyhow::Context;
use dbus::message::MatchRule;
use dbus_tokio::connection;
use featured::CheckFeature;
use featured::PlatformFeatures;
use fixedbitset::FixedBitSet;
use futures::prelude::*;
use once_cell::sync::Lazy;
use tokio::runtime;
use tokio::runtime::Runtime;
use tokio::task::AbortHandle;

use crate::cras_features_bindings;
use crate::decls;
use crate::Backend;

/// BackendFeatured is a feature Backend backed by dev-rust/featured.
pub struct BackendFeatured {
    cache: Arc<Mutex<FixedBitSet>>,
    rt: Option<Runtime>,
    abort_handle: AbortHandle,
}

static FEATURE_STORAGE: Lazy<Vec<featured::Feature>> = Lazy::new(|| {
    decls::FEATURES
        .iter()
        .map(|feature| {
            featured::Feature::new(feature.name, feature.default_enabled)
                .expect("featured::Feature::new")
        })
        .collect()
});

static FEATURE_LIST: Lazy<Vec<&'static featured::Feature>> =
    Lazy::new(|| FEATURE_STORAGE.iter().collect());

impl Backend for BackendFeatured {
    fn new(
        changed_callback: cras_features_bindings::cras_features_notify_changed,
    ) -> anyhow::Result<BackendFeatured> {
        let rt = runtime::Builder::new_multi_thread()
            .worker_threads(1)
            .thread_name("backend-featured")
            .enable_time()
            .enable_io()
            .build()
            .expect("runtime-build");
        let cache = get_default_cache();
        match watch_in_thread(&rt, cache.clone(), changed_callback) {
            Ok(abort_handle) => Ok(BackendFeatured {
                cache,
                rt: Some(rt),
                abort_handle,
            }),
            Err(e) => Err(e),
        }
    }

    fn is_enabled(&self, id: usize) -> bool {
        self.cache.lock().unwrap().contains(id)
    }
}

impl Drop for BackendFeatured {
    fn drop(&mut self) {
        self.abort_handle.abort();
        self.rt
            .take()
            .unwrap()
            .shutdown_timeout(Duration::from_secs(10));
    }
}

fn get_default_cache() -> Arc<Mutex<FixedBitSet>> {
    let mut cache = FixedBitSet::with_capacity(decls::FEATURES.len());

    for (i, feature) in decls::FEATURES.iter().enumerate() {
        cache.set(i, feature.default_enabled);
    }

    Arc::new(Mutex::new(cache))
}

fn fetch_features_blocking(features: &[&featured::Feature]) -> anyhow::Result<FixedBitSet> {
    let client = PlatformFeatures::get().unwrap();
    let status = client
        .get_params_and_enabled(features)
        .with_context(|| "get_params_and_enabled")?;

    let mut update = FixedBitSet::with_capacity(decls::FEATURES.len());
    for (i, feature) in features.iter().enumerate() {
        if status.get_params(feature).is_some() {
            update.set(i, true);
        }
    }
    Ok(update)
}

fn fetch_and_update_blocking(
    cache: &Arc<Mutex<FixedBitSet>>,
    changed_callback: cras_features_bindings::cras_features_notify_changed,
) {
    match fetch_features_blocking(&FEATURE_LIST) {
        Ok(update) => {
            let mut cache = cache.lock().unwrap();
            log::info!("features updated: {} (LSB first)", cache);
            if *cache != update {
                *cache = update;
                // SAFETY: Assume passed the callback is safe to call.
                unsafe {
                    changed_callback.unwrap()();
                }
            }
        }
        Err(e) => {
            log::error!("cannot fetch features: {e}");
        }
    }
}

/// watch_in_thread spawns a thread to watch for flag changes,
/// and returns an AbortHandle to stop watching.
/// The updates are written to the cache.
fn watch_in_thread(
    rt: &Runtime,
    cache: Arc<Mutex<FixedBitSet>>,
    changed_callback: cras_features_bindings::cras_features_notify_changed,
) -> anyhow::Result<AbortHandle> {
    let (resource, conn) =
        connection::new_system_sync().with_context(|| "connection::new_system_sync()")?;

    // https://docs.rs/dbus-tokio/latest/dbus_tokio/connection/
    // The resource is a task that should be spawned onto a tokio compatible
    // reactor ASAP. If the resource ever finishes, you lost connection to D-Bus.
    rt.spawn(async {
        let err = resource.await;
        log::error!("Lost connection to D-Bus: {}", err);
    });

    let mr = MatchRule::new_signal("org.chromium.feature_lib", "RefetchFeatureState");

    let join_handle = rt.spawn(async move {
        // Fetch on start up.
        fetch_and_update_blocking(&cache, changed_callback);

        let mut token = None;

        let result: anyhow::Result<()> = async {
            let (incoming_signal, stream) = conn
                .add_match(mr)
                .await
                .with_context(|| "conn.add_match")?
                .stream();
            token = Some(incoming_signal.token());

            stream
                .for_each(|(_, _): (_, ())| {
                    fetch_and_update_blocking(&cache, changed_callback);
                    async {}
                })
                .await;

            Ok(())
        }
        .await;

        if let Err(e) = result {
            log::error!("error occurred watching featured: {e:?}");
        }

        // Ensure the match is removed.
        // https://docs.rs/dbus/latest/dbus/nonblock/struct.MsgMatch.html#method.stream
        if let Some(token) = token {
            conn.remove_match(token)
                .await
                .unwrap_or_else(|e| log::error!("failed to remove match: {e:?}"));
        }
    });

    Ok(join_handle.abort_handle())
}
