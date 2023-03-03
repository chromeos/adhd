/* Copyright 2023 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
#include <featured/c_feature_library.h>
#include <stdbool.h>

#include "cras/src/server/cras_features_impl.h"

bool cras_features_backend_get_enabled(const struct cras_feature* feature) {
  const struct VariationsFeature featured_feature = {
      .name = feature->name,
      .default_state = feature->default_enabled ? FEATURE_ENABLED_BY_DEFAULT
                                                : FEATURE_DISABLED_BY_DEFAULT,
  };
  CFeatureLibrary lib = CFeatureLibraryNew();
  int enabled = CFeatureLibraryIsEnabledBlocking(lib, &featured_feature);
  CFeatureLibraryDelete(lib);

  return enabled;
}
