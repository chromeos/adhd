// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Generated from files in cras/src/server/rust in adhd.
// clang-format off

#ifdef __cplusplus
extern "C" {
#endif

#ifndef CRAS_SERVER_PLATFORM_DLC_DLC_H_
#define CRAS_SERVER_PLATFORM_DLC_DLC_H_

#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include "cras/common/rust_common.h"

/**
 * This type exists as an alternative to heap-allocated C-strings.
 *
 * This type, as a simple value, is free of ownership or memory leak issues,
 * when we pass this in a callback we don't have to worry about who should free the string.
 */
struct CrasDlcId128 {
  char id[128];
};

typedef int (*DlcInstallOnSuccessCallback)(struct CrasDlcId128 id, int32_t elapsed_seconds);

typedef int (*DlcInstallOnFailureCallback)(struct CrasDlcId128 id, int32_t elapsed_seconds);

/**
 * Returns `true` if sr-bt-dlc is available.
 */
bool cras_dlc_is_sr_bt_available(void);

/**
 * Returns the root path of sr-bt-dlc.
 * The returned string should be freed with cras_rust_free_string.
 */
char *cras_dlc_get_sr_bt_root_path(void);

/**
 * Overrides the DLC state for DLC `id`.
 *
 * # Safety
 * root_path must be a valid NULL terminated UTF-8 string.
 */
void cras_dlc_override_sr_bt_for_testing(bool installed, const char *root_path);

/**
 * Reset all DLC overrides.
 */
void cras_dlc_reset_overrides_for_testing(void);

/**
 * Start a thread to download all DLCs.
 */
void download_dlcs_until_installed_with_thread(DlcInstallOnSuccessCallback dlc_install_on_success_callback,
                                               DlcInstallOnFailureCallback dlc_install_on_failure_callback);

#endif  /* CRAS_SERVER_PLATFORM_DLC_DLC_H_ */

#ifdef __cplusplus
}
#endif
