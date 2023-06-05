/* Copyright 2023 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
#include <errno.h>
#include <featured/c_feature_library.h>
#include <stdbool.h>
#include <time.h>

#include "cras/platform/features/features_impl.h"

#define FEATURE_LIBRARY_TIMEOUT_MS 500
#define FEATURE_LIBRARY_CACHE_TTL_SEC 5

// c_feature_library does not recommend caching the results.
// However we can only use the blocking version and we have way too many
// D-Bus calls that we might block the main thread and CRAS clients,
// so we accept the tradeoffs that a stale cache may introduce.
// TODO(b/277860318): Properly ListenForRefetchNeeded().
static struct cached_feature {
  bool cache_value;
  time_t expires_sec;
} cached_features[NUM_FEATURES];

// c_feature_library expects the same `struct VariationsFeature` instance
// (same memory address) to be used to query a given feature.
// So we statically initialize them here instead of constructing dynamically
// inside cras_features_backend_get_enabled().
#define DEFINE_FEATURE(name, default_enabled)                   \
  [name] = {#name, default_enabled ? FEATURE_ENABLED_BY_DEFAULT \
                                   : FEATURE_DISABLED_BY_DEFAULT},
static const struct VariationsFeature variations_features[NUM_FEATURES] = {
#include "cras/platform/features/features.inc"
};
#undef DEFINE_FEATURE

int cras_features_init() {
  if (!CFeatureLibraryInitialize()) {
    // We don't really know what's going on. Just return an arbitrary error.
    return -ENODATA;
  }
  return 0;
}

void cras_features_deinit() {
  // Do nothing.
}

bool cras_features_backend_get_enabled(const struct cras_feature* feature) {
  const enum cras_feature_id id = cras_feature_get_id(feature);

  // Get current time.
  struct timespec ts;
  clock_gettime(CLOCK_MONOTONIC_RAW, &ts);

  // If cache is live, return it.
  if (ts.tv_sec < cached_features[id].expires_sec) {
    return cached_features[id].cache_value;
  }

  // Query the feature status.
  CFeatureLibrary lib = CFeatureLibraryGet();
  if (lib == NULL) {
    return variations_features[id].default_state;
  }

  int enabled = CFeatureLibraryIsEnabledBlockingWithTimeout(
      lib, &variations_features[id], FEATURE_LIBRARY_TIMEOUT_MS);

  // Set the cache value.
  cached_features[id].cache_value = enabled;
  cached_features[id].expires_sec = ts.tv_sec + FEATURE_LIBRARY_CACHE_TTL_SEC;
  return enabled;
}
