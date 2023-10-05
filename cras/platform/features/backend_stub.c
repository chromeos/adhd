// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stdbool.h>

#include "cras/platform/features/features_impl.h"

int cras_features_backend_init(cras_features_notify_changed changed_callback) {
  return 0;
}

void cras_features_backend_deinit() {
  // Do nothing.
}

bool cras_features_backend_get_enabled(const struct cras_feature* feature) {
  return feature->default_enabled;
}
