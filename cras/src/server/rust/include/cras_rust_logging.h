// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Generated from files in cras/src/server/rust in adhd.
// clang-format off

#ifdef __cplusplus
extern "C" {
#endif

#ifndef CRAS_SRC_SERVER_RUST_INCLUDE_CRAS_RUST_LOGGING_H_
#define CRAS_SRC_SERVER_RUST_INCLUDE_CRAS_RUST_LOGGING_H_

#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>

/**
 * Initialize logging for cras_rust.
 * Recommended to be called before all other cras_rust functions.
 */
int cras_rust_init_logging(void);

#endif /* CRAS_SRC_SERVER_RUST_INCLUDE_CRAS_RUST_LOGGING_H_ */

#ifdef __cplusplus
}
#endif
