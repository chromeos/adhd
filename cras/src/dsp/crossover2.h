/* Copyright 2013 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef CRAS_SRC_DSP_CROSSOVER2_H_
#define CRAS_SRC_DSP_CROSSOVER2_H_

#include "cras/src/dsp/biquad.h"
#include "cras/src/dsp/rust/dsp.h"

#ifdef __cplusplus
extern "C" {
#endif

int crossover2_convert_params_to_blob(const struct crossover2* xo2,
                                      int32_t* xo2_cfg);
#ifdef __cplusplus
}  // extern "C"
#endif

#endif  // CRAS_SRC_DSP_CROSSOVER2_H_
