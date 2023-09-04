// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stdbool.h>

#include "cras/platform/segmentation/segmentation.h"

bool cras_segmentation_enabled(const char* feature) {
  return false;
}
