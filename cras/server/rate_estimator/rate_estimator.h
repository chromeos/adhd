// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Generated from files in cras/src/server/rust in adhd.
// clang-format off

#ifdef __cplusplus
extern "C" {
#endif

#ifndef CRAS_SERVER_RATE_ESTIMATOR_RATE_ESTIMATOR_H_
#define CRAS_SERVER_RATE_ESTIMATOR_RATE_ESTIMATOR_H_

#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>

struct rate_estimator;

/**
 * # Safety
 *
 * To use this function safely, `window_size` must be a valid pointer to a
 * timespec.
 */
struct rate_estimator *rate_estimator_create(unsigned int rate,
                                             const struct timespec *window_size,
                                             double smooth_factor);

/**
 * Create a stub rate estimator for testing.
 */
struct rate_estimator *rate_estimator_create_stub(void);

/**
 * # Safety
 *
 * To use this function safely, `re` must be a pointer returned from
 * rate_estimator_create*, or null.
 */
void rate_estimator_destroy(struct rate_estimator *re);

/**
 * # Safety
 *
 * To use this function safely, `re` must be a pointer returned from
 * rate_estimator_create*, or null.
 */
bool rate_estimator_add_frames(struct rate_estimator *re, int frames);

/**
 * # Safety
 *
 * To use this function safely, `re` must be a pointer returned from
 * rate_estimator_create*, or null, and `now` must be a valid pointer to a
 * timespec.
 */
int32_t rate_estimator_check(struct rate_estimator *re, int level, const struct timespec *now);

/**
 * # Safety
 *
 * To use this function safely, `re` must be a pointer returned from
 * rate_estimator_create, or null.
 */
double rate_estimator_get_rate(const struct rate_estimator *re);

/**
 * # Safety
 *
 * To use this function safely, `re` must be a pointer returned from
 * rate_estimator_create, or null.
 */
void rate_estimator_reset_rate(struct rate_estimator *re, unsigned int rate);

/**
 * # Safety
 *
 * To use this function safely, `re` must be a pointer returned from
 * rate_estimator_create_stub.
 */
int32_t rate_estimator_get_last_add_frames_value_for_test(const struct rate_estimator *re);

/**
 * # Safety
 *
 * To use this function safely, `re` must be a pointer returned from
 * rate_estimator_create_stub.
 */
uint64_t rate_estimator_get_add_frames_called_count_for_test(const struct rate_estimator *re);

#endif  /* CRAS_SERVER_RATE_ESTIMATOR_RATE_ESTIMATOR_H_ */

#ifdef __cplusplus
}
#endif
