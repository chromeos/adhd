// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Generated from files in cras/src/server/rust in adhd.
// clang-format off

#ifdef __cplusplus
extern "C" {
#endif

#ifndef CRAS_SRC_DSP_RUST_DSP_H_
#define CRAS_SRC_DSP_RUST_DSP_H_

#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>

#define MAX_BIQUADS_PER_EQ 10

enum biquad_type {
  BQ_NONE,
  BQ_LOWPASS,
  BQ_HIGHPASS,
  BQ_BANDPASS,
  BQ_LOWSHELF,
  BQ_HIGHSHELF,
  BQ_PEAKING,
  BQ_NOTCH,
  BQ_ALLPASS,
};

struct dcblock;

struct eq;

struct biquad {
  float b0;
  float b1;
  float b2;
  float a1;
  float a2;
  float x1;
  float x2;
  float y1;
  float y2;
};

struct biquad biquad_new_set(enum biquad_type enum_type, double freq, double q, double gain);

struct dcblock *dcblock_new(void);

void dcblock_free(struct dcblock *dcblock);

void dcblock_set_config(struct dcblock *dcblock, float r, unsigned long sample_rate);

void dcblock_process(struct dcblock *dcblock, float *data, int32_t count);

struct eq *eq_new(void);

void eq_free(struct eq *eq);

int32_t eq_append_biquad(struct eq *eq,
                         enum biquad_type enum_type,
                         float freq,
                         float q,
                         float gain);

int32_t eq_append_biquad_direct(struct eq *eq, const struct biquad *biquad);

void eq_process(struct eq *eq, float *data, int32_t count);

struct biquad biquad_new_set(enum biquad_type enum_type, double freq, double q, double gain);

struct dcblock *dcblock_new(void);

void dcblock_free(struct dcblock *dcblock);

void dcblock_set_config(struct dcblock *dcblock, float r, unsigned long sample_rate);

void dcblock_process(struct dcblock *dcblock, float *data, int32_t count);

struct eq *eq_new(void);

void eq_free(struct eq *eq);

int32_t eq_append_biquad(struct eq *eq,
                         enum biquad_type enum_type,
                         float freq,
                         float q,
                         float gain);

int32_t eq_append_biquad_direct(struct eq *eq, const struct biquad *biquad);

void eq_process(struct eq *eq, float *data, int32_t count);

#endif /* CRAS_SRC_DSP_RUST_DSP_H_ */

#ifdef __cplusplus
}
#endif
