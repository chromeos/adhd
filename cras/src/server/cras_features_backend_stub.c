// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stdbool.h>

#include "cras/src/server/cras_features_impl.h"

bool cras_features_backend_get_enabled(const struct cras_feature* feature) {
  return feature->default_enabled;
}
