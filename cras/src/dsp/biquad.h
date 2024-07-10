/* Copyright 2013 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef CRAS_SRC_DSP_BIQUAD_H_
#define CRAS_SRC_DSP_BIQUAD_H_

#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>

#include "cras/src/dsp/rust/dsp.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Converts a biquad filter parameters to a blob for DSP offload.
 * Args:
 *    bq - The biquad filter we want to convert.
 *    bq_cfg - The pointer of the allocated blob buffer to be filled.
 *    gain_accum - The pointer of the gain running accumulation.
 *    dump_gain - false to keep accumulating the gain in gain_accum,
 *                true to dump the accumulated gain for now, putting it along
 *                with the output blob converted this time.
 * Returns:
 *    0 if the generation is successful. A negative error code otherwise.
 */
int biquad_convert_blob(struct biquad* bq,
                        int32_t* bq_cfg,
                        float* gain_accum,
                        bool dump_gain);
#ifdef __cplusplus
}  // extern "C"
#endif

#endif  // CRAS_SRC_DSP_BIQUAD_H_
