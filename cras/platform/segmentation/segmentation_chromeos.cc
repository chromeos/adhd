// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <libsegmentation/feature_management.h>

#include "cras/platform/segmentation/segmentation.h"

extern "C" bool cras_segmentation_enabled(const char* feature) {
  thread_local segmentation::FeatureManagement fm;
  return fm.IsFeatureEnabled(feature);
}
