// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Generated from files in cras/src/server/rust in adhd.
// clang-format off

#ifdef __cplusplus
extern "C" {
#endif

#ifndef CRAS_SRC_SERVER_RUST_INCLUDE_CRAS_DLC_H_
#define CRAS_SRC_SERVER_RUST_INCLUDE_CRAS_DLC_H_

#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>

/**
 * Returns `true` if the "sr-bt-dlc" packge is ready for use, otherwise
 * retuns `false`.
 */
bool cras_dlc_sr_bt_is_available(void);

/**
 * Returns Dlc root_path for the "sr-bt-dlc" package.
 */
const char *cras_dlc_sr_bt_get_root(void);

#endif // CRAS_SRC_SERVER_RUST_INCLUDE_CRAS_DLC_H_

#ifdef __cplusplus
}
#endif
