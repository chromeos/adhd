// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CRAS_SRC_SERVER_CRAS_DLC_MANAGER_H_
#define CRAS_SRC_SERVER_CRAS_DLC_MANAGER_H_

#include "cras/server/platform/dlc/dlc.h"

#ifdef __cplusplus
extern "C" {
#endif

void cras_dlc_manager_init(struct CrasDlcDownloadConfig dl_cfg);

#ifdef __cplusplus
}  // extern "C"
#endif

#endif  // CRAS_SRC_SERVER_CRAS_DLC_MANAGER_H_
