/* Copyright 2023 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef CRAS_INCLUDE_CRAS_TIMESPEC_H_
#define CRAS_INCLUDE_CRAS_TIMESPEC_H_

#include <stdint.h>
#include <time.h>

#ifdef __cplusplus
extern "C" {
#endif

// Architecture independent timespec
struct __attribute__((__packed__)) cras_timespec {
  int64_t tv_sec;
  int64_t tv_nsec;
};

// Converts a fixed-size cras_timespec to a time.h defined timespec
static inline void cras_timespec_to_timespec(struct timespec* dest,
                                             const struct cras_timespec* src) {
  dest->tv_sec = src->tv_sec;
  dest->tv_nsec = src->tv_nsec;
}

// Converts a time.h defined timespec to a fixed-size cras_timespec
static inline void cras_timespec_from_timespec(struct cras_timespec* dest,
                                               const struct timespec* src) {
  dest->tv_sec = src->tv_sec;
  dest->tv_nsec = src->tv_nsec;
}

#ifdef __cplusplus
}  // extern "C"
#endif

#endif  // CRAS_INCLUDE_CRAS_TIMESPEC_H_
