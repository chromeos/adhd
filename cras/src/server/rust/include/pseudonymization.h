// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Generated from files in cras/src/server/rust in adhd.
// clang-format off

#ifdef __cplusplus
extern "C" {
#endif

#ifndef CRAS_SRC_SERVER_RUST_INCLUDE_PSEUDONYMIZATION_H_
#define CRAS_SRC_SERVER_RUST_INCLUDE_PSEUDONYMIZATION_H_

#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>

/**
 * Pseudonymize the stable_id using the global salt.
 * Returns the salted stable_id.
 */
uint32_t pseudonymize_stable_id(uint32_t stable_id);

/**
 * Gets the salt from the environment variable CRAS_PSEUDONYMIZATION_SALT.
 * See `Salt::new_from_environment`.
 * Returns negative errno on failure.
 *
 * # Safety
 * salt must point to a non-NULL u32.
 */
int pseudonymize_salt_get_from_env(uint32_t *salt);

#endif /* CRAS_SRC_SERVER_RUST_INCLUDE_PSEUDONYMIZATION_H_ */

#ifdef __cplusplus
}
#endif
