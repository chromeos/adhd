/* Copyright 2013 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef CRAS_SRC_SERVER_SOFTVOL_CURVE_H_
#define CRAS_SRC_SERVER_SOFTVOL_CURVE_H_

#include <assert.h>
#include <math.h>
#include <sys/param.h>
#include <syslog.h>

#include "cras/src/server/cras_volume_curve.h"

#define LOG_10 2.302585

struct cras_volume_curve;

extern const float softvol_scalers[NUM_VOLUME_STEPS];

/* Returns the volume scaler in the soft volume curve for the given index. */
static inline float softvol_get_scaler_default(unsigned int volume_index) {
  volume_index = MIN(volume_index, MAX_VOLUME);
  return softvol_scalers[volume_index];
}

static inline float softvol_get_scaler(float* scalers,
                                       unsigned int volume_index) {
  assert(scalers);
  volume_index = MIN(volume_index, MAX_VOLUME);
  return scalers[volume_index];
}

// convert dBFS to softvol scaler
static inline float convert_softvol_scaler_from_dB(long dBFS) {
  return expf(LOG_10 * dBFS / 2000);
}

// The inverse function of convert_softvol_scaler_from_dB.
static inline long convert_dBFS_from_softvol_scaler(float scaler) {
  /*
   * Use lround here instead of direct casting to long to prevent
   * incorrect inversion.
   */
  return lround(logf(scaler) / LOG_10 * 2000);
}

// Builds software volume scalers from volume curve.
float* softvol_build_from_curve(const struct cras_volume_curve* curve);

#endif  // CRAS_SRC_SERVER_SOFTVOL_CURVE_H_
