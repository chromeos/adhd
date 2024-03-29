/* Copyright 2012 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "cras/src/server/cras_volume_curve.h"

#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <sys/param.h>

// Simple curve with configurable max volume and range.
struct stepped_curve {
  struct cras_volume_curve curve;
  long max_vol;
  long range;
};

static long get_dBFS_step(const struct cras_volume_curve* curve,
                          size_t volume) {
  const struct stepped_curve* c = (const struct stepped_curve*)curve;
  return c->max_vol - (c->range - c->range * volume / MAX_VOLUME);
}

// Curve that has each step explicitly called out by value.
struct explicit_curve {
  struct cras_volume_curve curve;
  long dB_values[NUM_VOLUME_STEPS];
};

static long get_dBFS_explicit(const struct cras_volume_curve* curve,
                              size_t volume) {
  const struct explicit_curve* c = (const struct explicit_curve*)curve;

  // Limit volume to (0, MAX_VOLUME).
  volume = MIN(MAX_VOLUME, MAX(0, volume));
  return c->dB_values[volume];
}

/*
 * Exported Interface.
 */

struct cras_volume_curve* cras_volume_curve_create_default() {
  // Default to max volume of 0dBFS, and a step of 0.5dBFS.
  return cras_volume_curve_create_simple_step(0, 5000);
}

struct cras_volume_curve* cras_volume_curve_create_simple_step(long max_volume,
                                                               long range) {
  struct stepped_curve* curve;
  curve = (struct stepped_curve*)calloc(1, sizeof(*curve));
  if (curve == NULL) {
    return NULL;
  }
  curve->curve.get_dBFS = get_dBFS_step;
  curve->max_vol = max_volume;
  curve->range = range;
  return &curve->curve;
}

struct cras_volume_curve* cras_volume_curve_create_explicit(
    long dB_values[NUM_VOLUME_STEPS]) {
  struct explicit_curve* curve;
  curve = (struct explicit_curve*)calloc(1, sizeof(*curve));
  if (curve == NULL) {
    return NULL;
  }
  curve->curve.get_dBFS = get_dBFS_explicit;
  memcpy(curve->dB_values, dB_values, sizeof(curve->dB_values));
  return &curve->curve;
}

void cras_volume_curve_destroy(struct cras_volume_curve* curve) {
  free(curve);
}
