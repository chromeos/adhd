// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Generated from files in cras/src/server/rust in adhd.
// clang-format off

#ifdef __cplusplus
extern "C" {
#endif

#ifndef CRAS_SRC_SERVER_RUST_INCLUDE_STRING_H_
#define CRAS_SRC_SERVER_RUST_INCLUDE_STRING_H_

#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>

/**
 * Free a string allocated from CRAS Rust functions.
 *
 * # Safety
 *
 * `s` must be a string allocated from CRAS Rust functions that asked for it to be freed
 * with this function.
 */
void cras_rust_free_string(char *s);

#endif /* CRAS_SRC_SERVER_RUST_INCLUDE_STRING_H_ */

#ifdef __cplusplus
}
#endif
