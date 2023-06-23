// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CRAS_SRC_SERVER_CRAS_DLC_MANAGER_TEST_ONLY_H_
#define CRAS_SRC_SERVER_CRAS_DLC_MANAGER_TEST_ONLY_H_

#include <stdbool.h>

#include "cras/src/server/cras_dlc_manager.h"

#ifdef __cplusplus
extern "C" {
#endif

bool cras_dlc_manager_is_null();

#ifdef __cplusplus
}  // extern "C"
#endif

#endif  // CRAS_SRC_SERVER_CRAS_DLC_MANAGER_TEST_ONLY_H_
