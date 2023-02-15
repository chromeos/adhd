// Copyright 2019 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Generated from files in cras/src/server/rust in adhd.
// clang-format off

#ifdef __cplusplus
extern "C" {
#endif

#ifndef CRAS_SRC_SERVER_RUST_INCLUDE_RATE_ESTIMATOR_H_
#define CRAS_SRC_SERVER_RUST_INCLUDE_RATE_ESTIMATOR_H_

#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <time.h>

/**
 * An estimator holding the required information to determine the actual frame
 * rate of an audio device.
 *
 * # Members
 *    * `last_level` - Buffer level of the audio device at last check time.
 *    * `level_diff` - Number of frames written to or read from audio device
 *                     since the last check time. Rate estimator will use this
 *                     change plus the difference of buffer level to derive the
 *                     number of frames audio device has actually processed.
 *    * `window_start` - The start time of the current window.
 *    * `window_size` - The size of the window.
 *    * `window_frames` - The number of frames accumulated in current window.
 *    * `lsq` - The helper used to estimate sample rate.
 *    * `smooth_factor` - A scaling factor used to average the previous and new
 *                        rate estimates to ensure that estimates do not change
 *                        too quickly.
 *    * `estimated_rate` - The estimated rate at which samples are consumed.
 */
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
 * # Safety
 *
 * To use this function safely, `re` must be a pointer returned from
 * rate_estimator_create, or null.
 */
void rate_estimator_destroy(struct rate_estimator *re);

/**
 * # Safety
 *
 * To use this function safely, `re` must be a pointer returned from
 * rate_estimator_create, or null.
 */
void rate_estimator_add_frames(struct rate_estimator *re, int frames);

/**
 * # Safety
 *
 * To use this function safely, `re` must be a pointer returned from
 * rate_estimator_create, or null, and `now` must be a valid pointer to a
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

#endif /* CRAS_SRC_SERVER_RUST_INCLUDE_RATE_ESTIMATOR_H_ */

#ifdef __cplusplus
}
#endif
