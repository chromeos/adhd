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

uint32_t pseudonymize_stable_id(uint32_t salt, uint32_t stable_id);

#endif /* CRAS_SRC_SERVER_RUST_INCLUDE_PSEUDONYMIZATION_H_ */

#ifdef __cplusplus
}
#endif
