// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CRAS_PLATFORM_SEGMENTATION_SEGMENTATION_H_
#define CRAS_PLATFORM_SEGMENTATION_SEGMENTATION_H_

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// Returns whether the named feature is enabled by segmentation.
//
// Same as the command: feature_explorer --feature_name=${feature}.
bool cras_segmentation_enabled(const char* feature);

#ifdef __cplusplus
}  // extern "C"
#endif

#endif  // CRAS_PLATFORM_SEGMENTATION_SEGMENTATION_H_
