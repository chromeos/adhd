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

struct CrasDlcDownloadConfig {
  bool dlcs_to_download[NUM_CRAS_DLCS];
};

typedef int (*CrasServerMetricsDlcInstallRetriedTimesOnSuccessFunc)(enum CrasDlcId, int32_t);

/**
 * Returns `true` if the DLC package is ready for use, otherwise
 * returns `false`.
 */
bool cras_dlc_is_available(enum CrasDlcId id);

/**
 * Returns the root path of the DLC package.
 * The returned string should be freed with cras_rust_free_string.
 */
char *cras_dlc_get_root_path(enum CrasDlcId id);

/**
 * Overrides the DLC state for DLC `id`.
 *
 * # Safety
 * root_path must be a valid NULL terminated UTF-8 string.
 */
void cras_dlc_override_state_for_testing(enum CrasDlcId id, bool installed, const char *root_path);

/**
 * Reset all DLC overrides.
 */
void cras_dlc_reset_overrides_for_testing(void);

/**
 * Start a thread to download all DLCs.
 */
void download_dlcs_until_installed_with_thread(struct CrasDlcDownloadConfig download_config,
                                               CrasServerMetricsDlcInstallRetriedTimesOnSuccessFunc cras_server_metrics_dlc_install_retried_times_on_success);

/**
 * Returns `true` if the DLC package is ready for use, otherwise
 * returns `false`.
 */
bool cras_dlc_is_available(enum CrasDlcId id);

/**
 * Returns the root path of the DLC package.
 * The returned string should be freed with cras_rust_free_string.
 */
char *cras_dlc_get_root_path(enum CrasDlcId id);

/**
 * Overrides the DLC state for DLC `id`.
 *
 * # Safety
 * root_path must be a valid NULL terminated UTF-8 string.
 */
void cras_dlc_override_state_for_testing(enum CrasDlcId id, bool installed, const char *root_path);

/**
 * Reset all DLC overrides.
 */
void cras_dlc_reset_overrides_for_testing(void);

/**
 * Start a thread to download all DLCs.
 */
void download_dlcs_until_installed_with_thread(struct CrasDlcDownloadConfig download_config,
                                               CrasServerMetricsDlcInstallRetriedTimesOnSuccessFunc cras_server_metrics_dlc_install_retried_times_on_success);

#endif /* CRAS_SERVER_PLATFORM_DLC_DLC_H_ */

#ifdef __cplusplus
}
#endif
