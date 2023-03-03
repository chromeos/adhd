/* Copyright 2021 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#define _GNU_SOURCE

#include "cras/src/common/cras_string.h"

#include <string.h>

const char* cras_strerror(int errnum) {
  static __thread char buf[1024];
  return strerror_r(errnum, buf, sizeof(buf));
}
