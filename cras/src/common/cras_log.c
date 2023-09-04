/* Copyright 2023 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "cras/src/common/cras_log.h"

#include <stdarg.h>
#include <stdio.h>
#include <threads.h>

#define MAX_LENGTH 256

#define TLS_MSG_COUNT 8

static thread_local char tlsmsg[TLS_MSG_COUNT][MAX_LENGTH] = {};

const char* tlsprintf(const char* fmt, ...) {
  static thread_local int i = 0;
  i = (i + 1) % TLS_MSG_COUNT;

  va_list args;
  va_start(args, fmt);
  vsnprintf(tlsmsg[i], sizeof(tlsmsg[i]), fmt, args);
  va_end(args);
  return tlsmsg[i];
}