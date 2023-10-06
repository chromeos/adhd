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
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>

#define CRAS_DLC_ID_STRING_MAX_LENGTH 50

/**
 * All supported DLCs in CRAS.
 */
enum CrasDlcId {
  CrasDlcSrBt,
  CrasDlcNcAp,
  NumCrasDlc,
};

/**
 * Returns `true` if the installation request is successfully sent,
 * otherwise returns `false`.
 */
bool cras_dlc_install(enum CrasDlcId id);

/**
 * Returns `true` if the DLC package is ready for use, otherwise
 * returns `false`.
 */
bool cras_dlc_is_available(enum CrasDlcId id);

/**
 * Returns the root path of the DLC package.
 *
 * # Safety
 *
 * This function leaks memory if called from C.
 * There is no valid way to free the returned string in C.
 * TODO(b/277566731): Fix it.
 */
const char *cras_dlc_get_root_path(enum CrasDlcId id);

/**
 * Writes the DLC ID string corresponding to the enum id to `ret`.
 * Suggested value of `ret_len` is `CRAS_DLC_ID_STRING_MAX_LENGTH`.
 *
 * # Safety
 * `ret` should have `ret_len` bytes writable.
 */
void cras_dlc_get_id_string(char *ret, size_t ret_len, enum CrasDlcId id);

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

#endif /* CRAS_SRC_SERVER_RUST_INCLUDE_CRAS_DLC_H_ */

#ifdef __cplusplus
}
#endif
