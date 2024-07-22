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

#define CROSSOVER2_NUM_LR4_PAIRS 3

#define MAX_BIQUADS_PER_EQ 10

/**
 * Maximum number of biquad filters an EQ2 can have per channel
 */
#define MAX_BIQUADS_PER_EQ2 10

#define EQ2_NUM_CHANNELS 2

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

/**
 * "eq2" is a two channel version of the "eq" filter. It processes two channels
 * of data at once to increase performance.
 */
struct eq2;

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
 *```text
 * x -- [BIQUAD] -- y -- [BIQUAD] -- z
 * ```
 *
 * Both biquad filter has the same parameter b[012] and a[12],
 * The variable [xyz][12][LR] keep the history values.
 *
 */
struct lr42 {
  float b0;
  float b1;
  float b2;
  float a1;
  float a2;
  float x1L;
  float x1R;
  float x2L;
  float x2R;
  float y1L;
  float y1R;
  float y2L;
  float y2R;
  float z1L;
  float z1R;
  float z2L;
  float z2R;
};

/**
 * Three bands crossover filter:
 *
 *```text
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
 *
 */
struct crossover2 {
  struct lr42 lp[CROSSOVER2_NUM_LR4_PAIRS];
  struct lr42 hp[CROSSOVER2_NUM_LR4_PAIRS];
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
 * "crossover2" is a two channel version of the "crossover" filter. It processes
 * two channels of data at once to increase performance.
 * Initializes a crossover2 filter
 * Args:
 *    xo2 - The crossover2 filter we want to initialize.
 *    freq1 - The normalized frequency splits low and mid band.
 *    freq2 - The normalized frequency splits mid and high band.
 *
 */
void crossover2_init(struct crossover2 *xo2, float freq1, float freq2);

/**
 * Splits input samples to three bands.
 * Args:
 *    xo2 - The crossover2 filter to use.
 *    count - The number of input samples.
 *    data0L, data0R - The input samples, also the place to store low band
 *                     output.
 *    data1L, data1R - The place to store mid band output.
 *    data2L, data2R - The place to store high band output.
 *
 */
void crossover2_process(struct crossover2 *xo2,
                        int32_t count,
                        float *data0L,
                        float *data0R,
                        float *data1L,
                        float *data1R,
                        float *data2L,
                        float *data2R);

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

/**
 * Create an EQ2.
 */
struct eq2 *eq2_new(void);

/**
 * Free an EQ.
 */
void eq2_free(struct eq2 *eq2);

/**
 * Append a biquad filter to an EQ2. An EQ2 can have at most MAX_BIQUADS_PER_EQ2
 * biquad filters per channel.
 * Args:
 *    eq2 - The EQ2 we want to use.
 *    channel - 0 or 1. The channel we want to append the filter to.
 *    type - The type of the biquad filter we want to append.
 *    frequency - The value should be in the range [0, 1]. It is relative to
 *        half of the sampling rate.
 *    Q, gain - The meaning depends on the type of the filter. See Web Audio
 *        API for details.
 * Returns:
 *    0 if success. -1 if the eq has no room for more biquads.
 *
 */
int32_t eq2_append_biquad(struct eq2 *eq2,
                          int32_t channel,
                          enum biquad_type enum_type,
                          float freq,
                          float q,
                          float gain);

/**
 * Append a biquad filter to an EQ2. An EQ2 can have at most MAX_BIQUADS_PER_EQ2
 * biquad filters. This is similar to eq2_append_biquad(), but it specifies the
 * biquad coefficients directly.
 * Args:
 *    eq2 - The EQ2 we want to use.
 *    channel - 0 or 1. The channel we want to append the filter to.
 *    biquad - The parameters for the biquad filter.
 * Returns:
 *    0 if success. -1 if the eq has no room for more biquads.
 *
 */
int32_t eq2_append_biquad_direct(struct eq2 *eq2, int32_t channel, struct biquad biquad);

/**
 * Process a buffer of audio data through the EQ2.
 * Args:
 *    eq2 - The EQ2 we want to use.
 *    data0 - The array of channel 0 audio samples.
 *    data1 - The array of channel 1 audio samples.
 *    count - The number of elements in each of the data array to process.
 *
 */
void eq2_process(struct eq2 *eq2, float *data0, float *data1, int32_t count);

/**
 * Get the number of biquads in the EQ2 channel.
 */
int32_t eq2_len(struct eq2 *eq2, int32_t channel);

/**
 * Get the biquad specified by index from the EQ2 channell
 */
struct biquad *eq2_get_bq(struct eq2 *eq2, int32_t channel, int32_t index);

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
 * "crossover2" is a two channel version of the "crossover" filter. It processes
 * two channels of data at once to increase performance.
 * Initializes a crossover2 filter
 * Args:
 *    xo2 - The crossover2 filter we want to initialize.
 *    freq1 - The normalized frequency splits low and mid band.
 *    freq2 - The normalized frequency splits mid and high band.
 *
 */
void crossover2_init(struct crossover2 *xo2, float freq1, float freq2);

/**
 * Splits input samples to three bands.
 * Args:
 *    xo2 - The crossover2 filter to use.
 *    count - The number of input samples.
 *    data0L, data0R - The input samples, also the place to store low band
 *                     output.
 *    data1L, data1R - The place to store mid band output.
 *    data2L, data2R - The place to store high band output.
 *
 */
void crossover2_process(struct crossover2 *xo2,
                        int32_t count,
                        float *data0L,
                        float *data0R,
                        float *data1L,
                        float *data1R,
                        float *data2L,
                        float *data2R);

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

/**
 * Create an EQ2.
 */
struct eq2 *eq2_new(void);

/**
 * Free an EQ.
 */
void eq2_free(struct eq2 *eq2);

/**
 * Append a biquad filter to an EQ2. An EQ2 can have at most MAX_BIQUADS_PER_EQ2
 * biquad filters per channel.
 * Args:
 *    eq2 - The EQ2 we want to use.
 *    channel - 0 or 1. The channel we want to append the filter to.
 *    type - The type of the biquad filter we want to append.
 *    frequency - The value should be in the range [0, 1]. It is relative to
 *        half of the sampling rate.
 *    Q, gain - The meaning depends on the type of the filter. See Web Audio
 *        API for details.
 * Returns:
 *    0 if success. -1 if the eq has no room for more biquads.
 *
 */
int32_t eq2_append_biquad(struct eq2 *eq2,
                          int32_t channel,
                          enum biquad_type enum_type,
                          float freq,
                          float q,
                          float gain);

/**
 * Append a biquad filter to an EQ2. An EQ2 can have at most MAX_BIQUADS_PER_EQ2
 * biquad filters. This is similar to eq2_append_biquad(), but it specifies the
 * biquad coefficients directly.
 * Args:
 *    eq2 - The EQ2 we want to use.
 *    channel - 0 or 1. The channel we want to append the filter to.
 *    biquad - The parameters for the biquad filter.
 * Returns:
 *    0 if success. -1 if the eq has no room for more biquads.
 *
 */
int32_t eq2_append_biquad_direct(struct eq2 *eq2, int32_t channel, struct biquad biquad);

/**
 * Process a buffer of audio data through the EQ2.
 * Args:
 *    eq2 - The EQ2 we want to use.
 *    data0 - The array of channel 0 audio samples.
 *    data1 - The array of channel 1 audio samples.
 *    count - The number of elements in each of the data array to process.
 *
 */
void eq2_process(struct eq2 *eq2, float *data0, float *data1, int32_t count);

/**
 * Get the number of biquads in the EQ2 channel.
 */
int32_t eq2_len(struct eq2 *eq2, int32_t channel);

/**
 * Get the biquad specified by index from the EQ2 channell
 */
struct biquad *eq2_get_bq(struct eq2 *eq2, int32_t channel, int32_t index);

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
