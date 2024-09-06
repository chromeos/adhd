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

/**
 * The number of compressor kernels (also the number of bands).
 */
#define DRC_NUM_KERNELS 3

/**
 * The number of stages for emphasis and deemphasis filters.
 */
#define DRC_EMPHASIS_NUM_STAGES 2

/**
 * The maximum number of frames can be passed to drc_process() call.
 */
#define DRC_PROCESS_MAX_FRAMES 2048

/**
 * The default value of PARAM_PRE_DELAY in seconds.
 */
#define DRC_DEFAULT_PRE_DELAY 0.006

#define DRC_NUM_CHANNELS 2

#define NEG_TWO_DB 0.7943282347242815

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

/**
 * DRC implements a flexible audio dynamics compression effect such as is
 * commonly used in musical production and game audio. It lowers the volume of
 * the loudest parts of the signal and raises the volume of the softest parts,
 * making the sound richer, fuller, and more controlled.
 *
 * This is a three band stereo DRC. There are three compressor kernels, and each
 * can have its own parameters. If a kernel is disabled, it only delays the
 * signal and does not compress it.
 *
 * ```text
 *                   INPUT
 *                     |
 *                +----------+
 *                | emphasis |
 *                +----------+
 *                     |
 *               +------------+
 *               | crossover  |
 *               +------------+
 *               /     |      \
 *      (low band) (mid band) (high band)
 *             /       |        \
 *         +------+ +------+ +------+
 *         |  drc | |  drc | |  drc |
 *         |kernel| |kernel| |kernel|
 *         +------+ +------+ +------+
 *              \      |        /
 *               \     |       /
 *              +-------------+
 *              |     (+)     |
 *              +-------------+
 *                     |
 *              +------------+
 *              | deemphasis |
 *              +------------+
 *                     |
 *                   OUTPUT
 * ```
 *
 * The parameters of the DRC compressor.
 *
 * PARAM_THRESHOLD - The value above which the compression starts, in dB.
 * PARAM_KNEE - The value above which the knee region starts, in dB.
 * PARAM_RATIO - The input/output dB ratio after the knee region.
 * PARAM_ATTACK - The time to reduce the gain by 10dB, in seconds.
 * PARAM_RELEASE - The time to increase the gain by 10dB, in seconds.
 * PARAM_PRE_DELAY - The lookahead time for the compressor, in seconds.
 * PARAM_RELEASE_ZONE[1-4] - The adaptive release curve parameters.
 * PARAM_POST_GAIN - The static boost value in output, in dB.
 * PARAM_FILTER_STAGE_GAIN - The gain of each emphasis filter stage.
 * PARAM_FILTER_STAGE_RATIO - The frequency ratio for each emphasis filter stage
 *     to the previous stage.
 * PARAM_FILTER_ANCHOR - The frequency of the first emphasis filter, in
 *     normalized frequency (in [0, 1], relative to half of the sample rate).
 * PARAM_CROSSOVER_LOWER_FREQ - The lower frequency of the band, in normalized
 *     frequency (in [0, 1], relative to half of the sample rate).
 * PARAM_ENABLED - 1 to enable the compressor, 0 to disable it.
 *
 */
enum drc_param {
  PARAM_THRESHOLD,
  PARAM_KNEE,
  PARAM_RATIO,
  PARAM_ATTACK,
  PARAM_RELEASE,
  PARAM_PRE_DELAY,
  PARAM_RELEASE_ZONE1,
  PARAM_RELEASE_ZONE2,
  PARAM_RELEASE_ZONE3,
  PARAM_RELEASE_ZONE4,
  PARAM_POST_GAIN,
  PARAM_FILTER_STAGE_GAIN,
  PARAM_FILTER_STAGE_RATIO,
  PARAM_FILTER_ANCHOR,
  PARAM_CROSSOVER_LOWER_FREQ,
  PARAM_ENABLED,
  PARAM_LAST,
};

struct dcblock;

struct drc;

struct drc_kernel;

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

/**
 * The structure is only used for exposing necessary components of drc to
 * FFI.
 *
 */
struct drc_component {
  /**
   * true to disable the emphasis and deemphasis, false to enable it.
   */
  bool emphasis_disabled;
  /**
   * parameters holds the tweakable compressor parameters.
   */
  float parameters[DRC_NUM_KERNELS][16];
  /**
   * The emphasis filter and deemphasis filter
   */
  const struct eq2 *emphasis_eq;
  const struct eq2 *deemphasis_eq;
  /**
   * The crossover filter
   */
  const struct crossover2 *xo2;
  /**
   * The compressor kernels
   */
  const struct drc_kernel *kernel[DRC_NUM_KERNELS];
};

struct drc_kernel_param {
  bool enabled;
  /**
   * Amount of input change in dB required for 1 dB of output change.
   * This applies to the portion of the curve above knee_threshold
   * (see below).
   *
   */
  float ratio;
  float slope;
  float linear_threshold;
  float db_threshold;
  /**
   * db_knee is the number of dB above the threshold before we enter the
   * "ratio" portion of the curve.  The portion between db_threshold and
   * (db_threshold + db_knee) is the "soft knee" portion of the curve
   * which transitions smoothly from the linear portion to the ratio
   * portion. knee_threshold is db_to_linear(db_threshold + db_knee).
   *
   */
  float db_knee;
  float knee_threshold;
  float ratio_base;
  /**
   * Internal parameter for the knee portion of the curve.
   */
  float K;
  /**
   * The release frames coefficients
   */
  float kA;
  float kB;
  float kC;
  float kD;
  float kE;
  /**
   * Calculated parameters
   */
  float main_linear_gain;
  float attack_frames;
  float sat_release_frames_inv_neg;
  float sat_release_rate_at_neg_two_db;
  float knee_alpha;
  float knee_beta;
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
 * Allocates a DRC.
 */
struct drc *drc_new(float sample_rate);

/**
 * Initializes a DRC.
 */
void drc_init(struct drc *drc);

/**
 * Frees a DRC.
 */
void drc_free(struct drc *drc);

/**
 * Processes input data using a DRC.
 * Args:
 *    drc - The DRC we want to use.
 *    float **data - Pointers to input/output data. The input must be stereo
 *        and one channel is pointed by data[0], another pointed by data[1]. The
 *        output data is stored in the same place.
 *    frames - The number of frames to process.
 *
 */
void drc_process(struct drc *drc, float **data, int32_t frames);

void drc_set_param(struct drc *drc, int32_t index, uint32_t paramID, float value);

/**
 * Retrive the components from a DRC structure
 * Args:
 *    drc - The DRC kernel.
 *
 */
struct drc_component drc_get_components(struct drc *drc);

void drc_set_emphasis_disabled(struct drc *drc, int32_t value);

/**
 * Initializes a drc kernel
 */
struct drc_kernel *dk_new(float sample_rate);

/**
 * Frees a drc kernel
 */
void dk_free(struct drc_kernel *dk);

/**
 * Sets the parameters of a drc kernel. See drc.h for details
 */
void dk_set_parameters(struct drc_kernel *dk,
                       float db_threshold,
                       float db_knee,
                       float ratio,
                       float attack_time,
                       float release_time,
                       float pre_delay_time,
                       float db_post_gain,
                       float releaseZone1,
                       float releaseZone2,
                       float releaseZone3,
                       float releaseZone4);

/**
 * Enables or disables a drc kernel
 */
void dk_set_enabled(struct drc_kernel *dk, int32_t enabled);

/**
 * Performs stereo-linked compression.
 * Args:
 *    dk - The DRC kernel.
 *    data - The pointers to the audio sample buffer. One pointer per channel.
 *    count - The number of audio samples per channel.
 *
 */
void dk_process(struct drc_kernel *dk, float **data_channels, uint32_t count);

/**
 * Retrieves and returns the parameters from a DRC kernel, `dk` must be a
 * pointer returned from dk_new.
 * Args:
 *    dk - The DRC kernel.
 *
 */
struct drc_kernel_param dk_get_parameter(const struct drc_kernel *dk);

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
int32_t eq2_len(const struct eq2 *eq2, int32_t channel);

/**
 * Get the biquad specified by index from the EQ2 channell
 */
const struct biquad *eq2_get_bq(const struct eq2 *eq2, int32_t channel, int32_t index);

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
 * Allocates a DRC.
 */
struct drc *drc_new(float sample_rate);

/**
 * Initializes a DRC.
 */
void drc_init(struct drc *drc);

/**
 * Frees a DRC.
 */
void drc_free(struct drc *drc);

/**
 * Processes input data using a DRC.
 * Args:
 *    drc - The DRC we want to use.
 *    float **data - Pointers to input/output data. The input must be stereo
 *        and one channel is pointed by data[0], another pointed by data[1]. The
 *        output data is stored in the same place.
 *    frames - The number of frames to process.
 *
 */
void drc_process(struct drc *drc, float **data, int32_t frames);

void drc_set_param(struct drc *drc, int32_t index, uint32_t paramID, float value);

/**
 * Retrive the components from a DRC structure
 * Args:
 *    drc - The DRC kernel.
 *
 */
struct drc_component drc_get_components(struct drc *drc);

void drc_set_emphasis_disabled(struct drc *drc, int32_t value);

/**
 * Initializes a drc kernel
 */
struct drc_kernel *dk_new(float sample_rate);

/**
 * Frees a drc kernel
 */
void dk_free(struct drc_kernel *dk);

/**
 * Sets the parameters of a drc kernel. See drc.h for details
 */
void dk_set_parameters(struct drc_kernel *dk,
                       float db_threshold,
                       float db_knee,
                       float ratio,
                       float attack_time,
                       float release_time,
                       float pre_delay_time,
                       float db_post_gain,
                       float releaseZone1,
                       float releaseZone2,
                       float releaseZone3,
                       float releaseZone4);

/**
 * Enables or disables a drc kernel
 */
void dk_set_enabled(struct drc_kernel *dk, int32_t enabled);

/**
 * Performs stereo-linked compression.
 * Args:
 *    dk - The DRC kernel.
 *    data - The pointers to the audio sample buffer. One pointer per channel.
 *    count - The number of audio samples per channel.
 *
 */
void dk_process(struct drc_kernel *dk, float **data_channels, uint32_t count);

/**
 * Retrieves and returns the parameters from a DRC kernel, `dk` must be a
 * pointer returned from dk_new.
 * Args:
 *    dk - The DRC kernel.
 *
 */
struct drc_kernel_param dk_get_parameter(const struct drc_kernel *dk);

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
int32_t eq2_len(const struct eq2 *eq2, int32_t channel);

/**
 * Get the biquad specified by index from the EQ2 channell
 */
const struct biquad *eq2_get_bq(const struct eq2 *eq2, int32_t channel, int32_t index);

struct eq *eq_new(void);

void eq_free(struct eq *eq);

int32_t eq_append_biquad(struct eq *eq,
                         enum biquad_type enum_type,
                         float freq,
                         float q,
                         float gain);

int32_t eq_append_biquad_direct(struct eq *eq, const struct biquad *biquad);

void eq_process(struct eq *eq, float *data, int32_t count);

#endif  /* CRAS_SRC_DSP_RUST_DSP_H_ */

#ifdef __cplusplus
}
#endif
