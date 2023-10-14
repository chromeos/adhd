/* Copyright 2023 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef CRAS_SRC_SERVER_CRAS_DSP_OFFLOAD_H_
#define CRAS_SRC_SERVER_CRAS_DSP_OFFLOAD_H_

#include "cras/src/server/cras_dsp_module.h"
#include "cras_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/* dsp_offload_map provides the mapping information of modules offloaded
 * from CRAS to DSP, which is instantiated per DSP pipeline. Given that DSP
 * pipeline and ALSA PCM endpoint are 1:1 mapped, so are ALSA PCM and CRAS
 * iodev, dsp_offload_map is thus instantiated per CRAS iodev (alsa_io to
 * be more accurate since the offload process is based on ALSA config).
 *
 * In practice, the dsp_offload_map instance will be owned by alsa_io device
 * individually. As soon as the complete init callback is processed on alsa_io
 * devices, dsp_offload_map should be instantiated on the demand of the board
 * config setting. The validity check should be run once instantiated, i.e.
 * probing every single mixer controls included in the offload map. The instance
 * should retain only when the validity check is passed, which implies the
 * capability of DSP offload on the specific alsa_io device.
 */
struct dsp_offload_map {
  // Member variables derived from the board config setting:
  // The associated pipeline index in DSP topology.
  uint32_t pipeline_id;
  // The string to describe the graph of DSP modules, e.g. "drc>eq2".
  const char* dsp_pattern;
};

/* Creates the offload map instance for the given type.
 * Args:
 *    offload_map - Pointer to the placeholder of the allocated instance.
 *    type - The representative node type.
 * Returns:
 *    0 in success or the negative error code.
 */
int cras_dsp_offload_create_map(struct dsp_offload_map** offload_map,
                                enum CRAS_NODE_TYPE type);

/* Configures the offload blob generated from the given module to the DSP mixer.
 * Args:
 *    offload_map - Pointer to the offload map instance.
 *    mod - Pointer to the CRAS-side DSP module instance.
 *    label - The string of label for the CRAS DSP plugin.
 * Returns:
 *    0 in success or the negative error code.
 */
int cras_dsp_offload_config_module(struct dsp_offload_map* offload_map,
                                   struct dsp_module* mod,
                                   const char* label);

/* Sets the offload mode and propagates to the asssociated DSP modules.
 * Args:
 *    offload_map - Pointer to the offload map instance.
 *    enabled - True to enable the offload; False to disable.
 * Returns:
 *    0 in success or the negative error code.
 */
int cras_dsp_offload_set_mode(struct dsp_offload_map* offload_map,
                              bool enabled);

#ifdef __cplusplus
}  // extern "C"
#endif

#endif  // CRAS_SRC_SERVER_CRAS_DSP_OFFLOAD_H_
