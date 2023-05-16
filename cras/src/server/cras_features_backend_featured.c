/* Copyright 2023 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
#include <featured/c_feature_library.h>
#include <stdbool.h>
#include <time.h>

#include "cras/src/server/cras_features_impl.h"

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
  const struct VariationsFeature featured_feature = {
      .name = feature->name,
      .default_state = feature->default_enabled ? FEATURE_ENABLED_BY_DEFAULT
                                                : FEATURE_DISABLED_BY_DEFAULT,
  };
  CFeatureLibrary lib = CFeatureLibraryNew();
  int enabled = CFeatureLibraryIsEnabledBlockingWithTimeout(
      lib, &featured_feature, FEATURE_LIBRARY_TIMEOUT_MS);
  CFeatureLibraryDelete(lib);

  // Set the cache value.
  cached_features[id].cache_value = enabled;
  cached_features[id].expires_sec = ts.tv_sec + FEATURE_LIBRARY_CACHE_TTL_SEC;
  return enabled;
}
