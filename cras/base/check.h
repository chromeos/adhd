// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CRAS_BASE_CHECK_H_
#define CRAS_BASE_CHECK_H_

#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

__attribute__((__noreturn__)) void cras_assertion_failure(const char* assertion,
                                                          const char* filename,
                                                          unsigned line,
                                                          const char* func);

// Our version of assert that is always executed regardless of NDEBUG.
#define CRAS_CHECK(expr)                                      \
  ((expr) ? (void)0                                           \
          : cras_assertion_failure(#expr, __FILE__, __LINE__, \
                                   __PRETTY_FUNCTION__))

#ifdef __cplusplus
}  // extern "C"
#endif

#endif  // CRAS_BASE_CHECK_H_
