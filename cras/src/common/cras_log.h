/* Copyright 2023 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef CRAS_SRC_COMMON_CRAS_LOG_H_
#define CRAS_SRC_COMMON_CRAS_LOG_H_

#ifdef __cplusplus
extern "C" {
#endif

#include "cras/common/rust_common.h"

// prints something to thread-local storage.
// Use only for printing debug messages.
const char* tlsprintf(const char* fmt, ...);

// Example:
//
// * FRALOG(USBAudioStartFailed, {"key1", "value1"}, {"key2", "value2"});
//
// Use tlsprintf to print non-string type values in one liner:
// * FRALOG(USBAudioStartFailed, {"key1", tlsprintf("%zx",int_value1)},
// {"key2", tlsprintf("%zx",int_value2)});
//
// It allows at most 8 tlsprintf calls within a FRALOG call.
#define FRALOG(signal, ...)                                                  \
  {                                                                          \
    const struct cras_fra_kv_t context[] = {__VA_ARGS__};                    \
    fralog(signal, sizeof(context) / sizeof(struct cras_fra_kv_t), context); \
  }

#ifdef __cplusplus
}  // extern "C"
#endif

#endif  // CRAS_SRC_COMMON_CRAS_LOG_H_
