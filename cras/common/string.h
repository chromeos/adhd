// Copyright 2025 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CRAS_COMMON_STRING_H_
#define CRAS_COMMON_STRING_H_

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

// Escape str. The returned string is NULL terminated. The returned string
// should be free()ed.
char* escape_string(const char* str, size_t len);

#ifdef __cplusplus
}  // extern "C"
#endif

#endif  // CRAS_COMMON_STRING_H_
