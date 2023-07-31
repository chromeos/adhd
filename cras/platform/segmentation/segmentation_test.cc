// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cras/platform/segmentation/segmentation.h"
#include "gtest/gtest.h"

TEST(CrasSegmentation, Smoke) {
  cras_segmentation_enabled("FeatureManagementAPNoiseCancellation");
}
