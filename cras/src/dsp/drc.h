/* Copyright 2013 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* Copyright 2011 Google LLC
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE.WEBKIT file.
 */

#ifndef CRAS_SRC_DSP_DRC_H_
#define CRAS_SRC_DSP_DRC_H_

#ifdef __cplusplus
extern "C" {
#endif

#include "cras/src/dsp/crossover2.h"
#include "cras/src/dsp/eq2.h"
#include "cras/src/dsp/rust/dsp.h"

/* Converts the parameter set of a DRC to the config blob for DSP offload.
 * Args:
 *    drc - The DRC we want to use.
 *    config - The pointer of the config blob buffer to be returned.
 *    config_size - The config blob size in bytes.
 * Returns:
 *    0 if the conversion is successful. A negative error code otherwise.
 */
int drc_convert_params_to_blob(struct drc* drc,
                               uint32_t** config,
                               size_t* config_size);
#ifdef __cplusplus
}  // extern "C"
#endif

#endif  // CRAS_SRC_DSP_DRC_H_
