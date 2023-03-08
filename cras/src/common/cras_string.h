/* Copyright 2021 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef CRAS_SRC_COMMON_CRAS_STRING_H_
#define CRAS_SRC_COMMON_CRAS_STRING_H_

#include <stdbool.h>
#include <string.h>

#include "third_party/strlcpy/strlcpy.h"

#ifdef __cplusplus
extern "C" {
#endif

// Therad safe version of strerror(3)
const char* cras_strerror(int errnum);

static inline bool str_has_prefix(const char* str, const char* prefix) {
  return 0 == strncmp(str, prefix, strlen(prefix));
}

#ifdef __cplusplus
}  // extern "C"
#endif

#endif  // CRAS_SRC_COMMON_CRAS_STRING_H_
