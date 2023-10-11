/* Copyright 2023 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef CRAS_SRC_SERVER_CRAS_DSP_OFFLOAD_H_
#define CRAS_SRC_SERVER_CRAS_DSP_OFFLOAD_H_

#include "cras/src/server/cras_dsp_module.h"

#ifdef __cplusplus
extern "C" {
#endif

void cras_dsp_offload_init();

/* Configures the offloading blob generated from the given DSP module (defined
 * in CRAS) to the corresponding offload DSP mixer control.
 *
 * Note: It now assumes that modules for offload are located on DSP pipeline
 *       idx=1 with the fixed component idx=0. Both indices will be configurable
 *       via the extra arguments in later CLs.
 */
int cras_dsp_offload_config_module(struct dsp_module* mod, const char* label);

/* Toggles the enabled/disabled mode for the given DSP module.
 *
 * Note: It now assumes that modules for offload are located on DSP pipeline
 *       idx=1 with the fixed component idx=0. Both indices will be configurable
 *       via the extra arguments in later CLs.
 */
int cras_dsp_offload_set_mode(bool enabled, const char* label);

#ifdef __cplusplus
}  // extern "C"
#endif

#endif  // CRAS_SRC_SERVER_CRAS_DSP_OFFLOAD_H_
