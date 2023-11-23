/* Copyright 2023 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef CRAS_SRC_SERVER_CRAS_DSP_OFFLOAD_H_
#define CRAS_SRC_SERVER_CRAS_DSP_OFFLOAD_H_

#include "cras/src/server/cras_dsp_module.h"
#include "cras_iodev_info.h"
#include "cras_types.h"

#ifdef __cplusplus
extern "C" {
#endif

struct cras_iodev;
struct cras_ionode;

/* The maximum size of the DSP pattern string for a pipeline. Please see
 * cras_dsp_pipeline_get_pattern() in cras_dsp_pipeline for more details.
 */
#define DSP_PATTERN_MAX_SIZE 100

/* The default DSP pattern for a pipeline which is the expected pattern for
 * DRC/EQ offload application.
 */
#define DSP_PATTERN_OFFLOAD_DEFAULT "drc>eq2"

/* dsp_offload_map provides the mapping information of modules offloaded
 * from CRAS to DSP, which is instantiated per DSP pipeline. Given that DSP
 * pipeline and ALSA PCM endpoint are 1:1 mapped, so are ALSA PCM and CRAS
 * iodev, dsp_offload_map is thus instantiated per CRAS iodev.
 *
 * In practice, the dsp_offload_map instance will be owned by alsa_io
 * individually (stored in base cras_iodev). As soon as the complete init
 * callback is processed on alsa_io devices, dsp_offload_map should be
 * instantiated on the demand of the board config setting. The validity check
 * should be run once instantiated, i.e. probing every single mixer controls
 * included in the offload map. The instance should retain only when the
 * validity check is passed, which implies the capability of DSP offload on the
 * specific alsa_io device.
 *
 * The offload process on a pipeline is applied in runtime with "all-or-none"
 * rule. That is, it only allows to offload all modules along the CRAS pipeline
 * to DSP at once; otherwise, the CRAS pipeline should keep as is without the
 * DSP offload support (offload is in disabled state, i.e. CRAS pipeline is
 * unchanged while DSP pipeline runs in bypass mode). Such states are stored by
 * variables in dsp_offload_map as well. They are critical for offload logic to
 * determine the required action(s) to do on state transitions.
 */
struct dsp_offload_map {
  // Member variables derived from the board config setting:
  // The associated pipeline index in DSP topology.
  uint32_t pipeline_id;
  // The string to describe the graph of DSP modules, e.g. "drc>eq2".
  char* dsp_pattern;

  // The iodev pointer which this map belongs to. This is only for referencing
  // the active node index while processing the DSP offload.
  const struct cras_iodev* parent_dev;

  // State variables which can be changed dynamically in runtime:
  // The working state of DSP processing.
  enum CRAS_DSP_PROC_STATE state;
  // The node index that DSP offload is currently applied for. This is valid
  // only if state == DSP_PROC_ON_DSP (offload applied).
  uint32_t applied_node_idx;
};

/* Creates the offload map for the device where the given node belongs to.
 * Args:
 *    offload_map - Pointer to the placeholder of the allocated instance.
 *    node - The representative node instance.
 * Returns:
 *    If the given node is not a candidate of support, returns 0 while keeping
 *    offload_map as NULL.
 *    Otherwise, returns 0 if reated in success or the negative error code.
 */
int cras_dsp_offload_create_map(struct dsp_offload_map** offload_map,
                                const struct cras_ionode* node);

/* Checks if the offload is already applied for the requested node.
 * Args:
 *    offload_map - Pointer to the offload map instance.
 * Returns:
 *    A boolean.
 */
bool cras_dsp_offload_is_already_applied(struct dsp_offload_map* offload_map);

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

/* Sets the offload state and propagates to the associated DSP modules.
 * Args:
 *    offload_map - Pointer to the offload map instance.
 *    enabled - True to enable the offload; False to disable.
 * Returns:
 *    0 in success or the negative error code.
 */
int cras_dsp_offload_set_state(struct dsp_offload_map* offload_map,
                               bool enabled);

/* Resets the state of the offload map. This is only for cmd_reload_ini use.
 * Args:
 *    offload_map - Pointer to the offload map instance.
 */
void cras_dsp_offload_reset_map(struct dsp_offload_map* offload_map);

/* Frees memory of the offload map.
 * Args:
 *    offload_map - Pointer to the offload map instance.
 */
void cras_dsp_offload_free_map(struct dsp_offload_map* offload_map);

#ifdef __cplusplus
}  // extern "C"
#endif

#endif  // CRAS_SRC_SERVER_CRAS_DSP_OFFLOAD_H_
