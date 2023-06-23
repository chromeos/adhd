// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CRAS_SRC_SERVER_CRAS_FEATURE_MONITOR_H_
#define CRAS_SRC_SERVER_CRAS_FEATURE_MONITOR_H_

#ifdef __cplusplus
extern "C" {
#endif

// Initializes feature monitor and sets main thread callback.
int cras_feature_monitor_init();

#ifdef __cplusplus
}  // extern "C"
#endif

#endif
