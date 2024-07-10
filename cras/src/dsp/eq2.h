/* Copyright 2013 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef CRAS_SRC_DSP_EQ2_H_
#define CRAS_SRC_DSP_EQ2_H_

#include "cras/src/dsp/biquad.h"
#include "cras/src/dsp/rust/dsp.h"

#ifdef __cplusplus
extern "C" {
#endif

/* "eq2" is a two channel version of the "eq" filter. It processes two channels
 * of data at once to increase performance. */

// Maximum number of biquad filters an EQ2 can have per channel
#define MAX_BIQUADS_PER_EQ2 10

struct eq2;

// Create an EQ2.
struct eq2* eq2_new();

// Free an EQ2.
void eq2_free(struct eq2* eq2);

/* Append a biquad filter to an EQ2. An EQ2 can have at most MAX_BIQUADS_PER_EQ2
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
 */
int eq2_append_biquad(struct eq2* eq2,
                      int channel,
                      enum biquad_type type,
                      float freq,
                      float Q,
                      float gain);

/* Append a biquad filter to an EQ2. An EQ2 can have at most MAX_BIQUADS_PER_EQ2
 * biquad filters. This is similar to eq2_append_biquad(), but it specifies the
 * biquad coefficients directly.
 * Args:
 *    eq2 - The EQ2 we want to use.
 *    channel - 0 or 1. The channel we want to append the filter to.
 *    biquad - The parameters for the biquad filter.
 * Returns:
 *    0 if success. -1 if the eq has no room for more biquads.
 */
int eq2_append_biquad_direct(struct eq2* eq2,
                             int channel,
                             const struct biquad* biquad);

/* Process a buffer of audio data through the EQ2.
 * Args:
 *    eq2 - The EQ2 we want to use.
 *    data0 - The array of channel 0 audio samples.
 *    data1 - The array of channel 1 audio samples.
 *    count - The number of elements in each of the data array to process.
 */
void eq2_process(struct eq2* eq2, float* data0, float* data1, int count);

/* Convert the biquad array on the given channel to the config blob.
 * Args:
 *    eq2 - The EQ2 we want to use.
 *    bq_cfg - The pointer of the config blob buffer to be filled.
 *    channel - The channel to convert.
 * Returns:
 *    0 if the generation is successful. A negative error code otherwise.
 */
int eq2_convert_channel_response(struct eq2* eq2, int32_t* bq_cfg, int channel);

/* Convert the parameter set of an EQ2 to the config blob for DSP offload.
 * Args:
 *    eq2 - The EQ2 we want to use.
 *    config - The pointer of the config blob buffer to be returned.
 *    config_size - The config blob size in bytes.
 * Returns:
 *    0 if the conversion is successful. A negative error code otherwise.
 */
int eq2_convert_params_to_blob(struct eq2* eq2,
                               uint32_t** config,
                               size_t* config_size);
#ifdef __cplusplus
}  // extern "C"
#endif

#endif  // CRAS_SRC_DSP_EQ2_H_
