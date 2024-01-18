/* Copyright 2021 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef CRAS_SRC_COMMON_CRAS_STRING_H_
#define CRAS_SRC_COMMON_CRAS_STRING_H_

#include <errno.h>
#include <stdbool.h>
#include <stdlib.h>
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

// Use this with presumption that str1 and/or str2 is null-terminated.
// e.g. compare to string literal: str_equals(s, "foo")
static inline bool str_equals(const char* str1, const char* str2) {
  if (!str1 || !str2) {
    return false;
  }

  return !strcmp(str1, str2);
}

// Use this when neither str1 nor str2 is guaranteed to be null-terminated.
// Note this differs from strncmp which is compared within a range. Instead,
// this returns false if either str1 or str2 is not null-terminated within max
// characters.
static inline bool str_equals_bounded(const char* str1,
                                      const char* str2,
                                      size_t max) {
  if (!str1 || !str2) {
    return false;
  }

  return !strncmp(str1, str2, max) && memchr(str1, 0, max) &&
         memchr(str2, 0, max);
}

// Convert string to int. This function is a wrapper for strtol;
static __attribute__((warn_unused_result)) inline int parse_int(const char* str,
                                                                int* out) {
  if (!str || !out) {
    return -EINVAL;
  }
  char* endptr;
  errno = 0;

  int num = strtol(str, &endptr, 10);
  if (endptr == str) {
    return -EINVAL;
  }
  *out = num;

  return -errno;
}

// Convert string to unsigned long. This function is a wrapper for strtoul;
static __attribute__((warn_unused_result)) inline int parse_unsigned_long(
    const char* str,
    unsigned long* out) {
  if (!str || !out) {
    return -EINVAL;
  }
  char* endptr;
  errno = 0;

  unsigned long num = strtoul(str, &endptr, 10);
  if (endptr == str) {
    return -EINVAL;
  }
  *out = num;

  return -errno;
}

// Convert string to float. This function is a wrapper for strtof.
static __attribute__((warn_unused_result)) inline int parse_float(
    const char* str,
    float* out) {
  if (!str || !out) {
    return -EINVAL;
  }
  char* endptr;
  errno = 0;
  float f = strtof(str, &endptr);
  if (endptr == str) {
    return -EINVAL;
  }
  *out = f;
  return -errno;
}

// Convert string to double. This function is a wrapper for strtod.
static __attribute__((warn_unused_result)) inline int parse_double(
    const char* str,
    double* out) {
  if (!str || !out) {
    return -EINVAL;
  }
  char* endptr;
  errno = 0;
  double d = strtod(str, &endptr);
  if (endptr == str) {
    return -EINVAL;
  }
  *out = d;
  return -errno;
}

#ifdef __cplusplus
}  // extern "C"
#endif

#endif  // CRAS_SRC_COMMON_CRAS_STRING_H_
