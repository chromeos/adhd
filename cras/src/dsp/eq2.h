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

/* Convert the biquad array on the given channel to the config blob.
 * Args:
 *    eq2 - The EQ2 we want to use.
 *    bq_cfg - The pointer of the config blob buffer to be filled.
 *    channel - The channel to convert.
 * Returns:
 *    0 if the generation is successful. A negative error code otherwise.
 */
int eq2_convert_channel_response(const struct eq2* eq2,
                                 int32_t* bq_cfg,
                                 int channel);

/* Convert the parameter set of an EQ2 to the config blob for DSP offload.
 * Args:
 *    eq2 - The EQ2 we want to use.
 *    config - The pointer of the config blob buffer to be returned.
 *    config_size - The config blob size in bytes.
 * Returns:
 *    0 if the conversion is successful. A negative error code otherwise.
 */
int eq2_convert_params_to_blob(const struct eq2* eq2,
                               uint32_t** config,
                               size_t* config_size);
#ifdef __cplusplus
}  // extern "C"
#endif

#endif  // CRAS_SRC_DSP_EQ2_H_
