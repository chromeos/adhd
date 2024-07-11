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

/**
 * An LR4 filter is two biquads with the same parameters connected in series:
 *
 * ```text
 * x -- [BIQUAD] -- y -- [BIQUAD] -- z
 * ```
 *
 * Both biquad filter has the same parameter b[012] and a[12],
 * The variable [xyz][12] keep the history values.
 */
struct LR4 {
  float b0;
  float b1;
  float b2;
  float a1;
  float a2;
  float x1;
  float x2;
  float y1;
  float y2;
  float z1;
  float z2;
};

/**
 * Three bands crossover filter:
 *
 * ```text
 * INPUT --+-- lp0 --+-- lp1 --+---> LOW (0)
 *         |         |         |
 *         |         \-- hp1 --/
 *         |
 *         \-- hp0 --+-- lp2 ------> MID (1)
 *                   |
 *                   \-- hp2 ------> HIGH (2)
 *
 *            [f0]       [f1]
 * ```
 *
 * Each lp or hp is an LR4 filter, which consists of two second-order
 * lowpass or highpass butterworth filters.
 */
struct crossover {
  struct LR4 lp[3];
  struct LR4 hp[3];
};

struct biquad biquad_new_set(enum biquad_type enum_type, double freq, double q, double gain);

/**
 * Initializes a crossover filter
 * Args:
 *    xo - The crossover filter we want to initialize.
 *    freq1 - The normalized frequency splits low and mid band.
 *    freq2 - The normalized frequency splits mid and high band.
 */
void crossover_init(struct crossover *xo, float freq1, float freq2);

/**
 * Splits input samples to three bands.
 * Args:
 *    xo - The crossover filter to use.
 *    count - The number of input samples.
 *    data0 - The input samples, also the place to store low band output.
 *    data1 - The place to store mid band output.
 *    data2 - The place to store high band output.
 */
void crossover_process(struct crossover *xo,
                       int32_t count,
                       float *data0,
                       float *data1,
                       float *data2);

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

/**
 * Initializes a crossover filter
 * Args:
 *    xo - The crossover filter we want to initialize.
 *    freq1 - The normalized frequency splits low and mid band.
 *    freq2 - The normalized frequency splits mid and high band.
 */
void crossover_init(struct crossover *xo, float freq1, float freq2);

/**
 * Splits input samples to three bands.
 * Args:
 *    xo - The crossover filter to use.
 *    count - The number of input samples.
 *    data0 - The input samples, also the place to store low band output.
 *    data1 - The place to store mid band output.
 *    data2 - The place to store high band output.
 */
void crossover_process(struct crossover *xo,
                       int32_t count,
                       float *data0,
                       float *data1,
                       float *data2);

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
