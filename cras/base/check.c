/*
 * Copyright 2023 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "cras/base/check.h"

#include <stdio.h>
#include <stdlib.h>

__attribute__((__noreturn__)) void cras_assertion_failure(const char* assertion,
                                                          const char* filename,
                                                          unsigned line,
                                                          const char* func) {
  fprintf(stderr, "%s: %u: Assertion failed: '%s' in function: %s\n", filename,
          line, assertion, func);
  abort();
}
