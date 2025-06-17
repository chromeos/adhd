// Copyright 2025 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cras/common/string.h"

#include <stdio.h>

#include "cras/common/check.h"

char* escape_string(const char* str, size_t len) {
  char* out;
  size_t outsize;
  FILE* stream = open_memstream(&out, &outsize);
  CRAS_CHECK(stream);
  for (size_t i = 0; i < len; i++) {
    char c = str[i];
    if (c < ' ' || c > '~') {
      CRAS_CHECK(fprintf(stream, "\\x%02x", c) > 0);
    } else {
      CRAS_CHECK(fputc(c, stream) != EOF);
    }
  }
  CRAS_CHECK(fclose(stream) == 0);
  return out;
}
