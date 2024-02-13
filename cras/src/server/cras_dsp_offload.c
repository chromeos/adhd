/* Copyright 2023 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef _GNU_SOURCE
#define _GNU_SOURCE  // For asprintf
#endif

#include "cras/src/server/cras_dsp_offload.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/param.h>
#include <syslog.h>

#include "cras/src/common/cras_string.h"
#include "cras/src/server/cras_alsa_config.h"
#include "cras/src/server/cras_iodev.h"
#include "cras/src/server/cras_system_state.h"
#include "cras_util.h"

// DSP module offload APIs

/* Probes the DSP module mixer controls for the given ids.
 * Args:
 *    pipeline_id - The pipeline id which the DSP module locates on.
 *    comp_id - The component id of the DSP module.
 * Returns:
 *    0 in success or the negative error code.
 */
typedef int (*probe_t)(uint32_t pipeline_id, uint32_t comp_id);

/* Sets the config blob to offload the given CRAS module to DSP.
 * Args:
 *    module - The pointer of the corresponding dsp_module defined in CRAS.
 *    pipeline_id - The pipeline id which the DSP module locates on.
 *    comp_id - The component id of the DSP module.
 * Returns:
 *    0 in success or the negative error code.
 */
typedef int (*set_offload_blob_t)(struct dsp_module* module,
                                  uint32_t pipeline_id,
                                  uint32_t comp_id);

/* Sets the offload mode to the corresponding module on DSP.
 * Args:
 *    enabled - True to run with the config; false to run in bypass mode.
 *    pipeline_id - The pipeline id which the DSP module locates on.
 *    comp_id - The component id of the DSP module.
 * Returns:
 *    0 in success or the negative error code.
 */
typedef int (*set_offload_mode_t)(bool enabled,
                                  uint32_t pipeline_id,
                                  uint32_t comp_id);

// The DSP module offload API set for a certain module label.
struct dsp_module_offload_api {
  const char* label;                    // align to the CRAS DSP module plugin
  probe_t probe;                        // probe control function
  set_offload_blob_t set_offload_blob;  // blob control function
  set_offload_mode_t set_offload_mode;  // enable control function
};

// DSP module offload API set implementation

static int module_set_offload_blob(struct dsp_module* module,
                                   const char* mixer_name) {
  if (!module || !mixer_name) {
    return -ENOENT;
  }

  uint32_t* blob;
  size_t blob_size;
  int rc = module->get_offload_blob(module, &blob, &blob_size);
  if (rc) {
    syslog(LOG_ERR, "set_offload_blob: Failed to get offload blob");
    return rc;
  }

  rc = cras_alsa_config_set_tlv_bytes(mixer_name, (uint8_t*)blob, blob_size);
  if (rc) {
    syslog(LOG_ERR, "set_offload_blob: Failed to set blob for DSP offload");
  }

  free(blob);
  return rc;
}

/* Mixer names vary according to SOF IPC versions. The ones specified below are
 * applied on IPC3 (adopted by pre-MTL DSP now), while IPC4 is available and
 * will be adopted by MTL.
 * TODO(b/188647460): support both mixer names on IPC3 and IPC4 as needed.
 */
static char* drc_blob_control_name(uint32_t pipeline_id, uint32_t comp_id) {
  char* mixer_name;
  if (asprintf(&mixer_name, "MULTIBAND_DRC%u.%u multiband_drc_control_%u",
               pipeline_id, comp_id, pipeline_id) == -1) {
    return NULL;
  }
  return mixer_name;
}

static char* drc_enable_control_name(uint32_t pipeline_id, uint32_t comp_id) {
  char* mixer_name;
  if (asprintf(&mixer_name, "MULTIBAND_DRC%u.%u multiband_drc_enable_%u",
               pipeline_id, comp_id, pipeline_id) == -1) {
    return NULL;
  }
  return mixer_name;
}

static int drc_probe(uint32_t pipeline_id, uint32_t comp_id) {
  char* mixer_name = drc_blob_control_name(pipeline_id, comp_id);
  if (!mixer_name) {
    syslog(LOG_ERR, "drc_probe: Error creating mixer name");
    return -ENOMEM;
  }

  // Probe the blob type mixer control.
  int rc = cras_alsa_config_probe(mixer_name);
  free(mixer_name);
  if (rc) {
    syslog(LOG_INFO, "drc_probe: Blob control is not detected");
    return rc;
  }

  mixer_name = drc_enable_control_name(pipeline_id, comp_id);
  if (!mixer_name) {
    syslog(LOG_ERR, "drc_probe: Error creating mixer name");
    return -ENOMEM;
  }

  // Probe the switch type mixer control.
  rc = cras_alsa_config_probe(mixer_name);
  free(mixer_name);
  if (rc) {
    syslog(LOG_INFO, "drc_probe: Enable control is not detected");
  }
  return rc;
}

static int drc_set_offload_blob(struct dsp_module* module,
                                uint32_t pipeline_id,
                                uint32_t comp_id) {
  char* mixer_name = drc_blob_control_name(pipeline_id, comp_id);
  if (!mixer_name) {
    syslog(LOG_ERR, "drc_set_offload_blob: Error creating mixer name");
    return -ENOMEM;
  }

  int rc = module_set_offload_blob(module, mixer_name);
  free(mixer_name);
  if (rc) {
    syslog(LOG_ERR, "drc_set_offload_blob: Error setting offload blob");
  }
  return rc;
}

static int drc_set_offload_mode(bool enabled,
                                uint32_t pipeline_id,
                                uint32_t comp_id) {
  char* mixer_name = drc_enable_control_name(pipeline_id, comp_id);
  if (!mixer_name) {
    syslog(LOG_ERR, "drc_set_offload_mode: Error creating mixer name");
    return -ENOMEM;
  }

  int rc = cras_alsa_config_set_switch(mixer_name, enabled);
  free(mixer_name);
  if (rc) {
    syslog(LOG_ERR, "drc_set_offload_mode: Error setting offload mode");
  }
  return rc;
}

static char* eq2_blob_control_name(uint32_t pipeline_id, uint32_t comp_id) {
  char* mixer_name;
  if (asprintf(&mixer_name, "EQIIR%u.%u eq_iir_control_%u", pipeline_id,
               comp_id, pipeline_id) == -1) {
    return NULL;
  }
  return mixer_name;
}

static int eq2_probe(uint32_t pipeline_id, uint32_t comp_id) {
  char* mixer_name = eq2_blob_control_name(pipeline_id, comp_id);
  if (!mixer_name) {
    syslog(LOG_ERR, "eq2_probe: Error creating mixer name");
    return -ENOMEM;
  }

  // Probe the blob type mixer control.
  int rc = cras_alsa_config_probe(mixer_name);
  free(mixer_name);
  if (rc) {
    syslog(LOG_INFO, "eq2_probe: Blob control is not detected");
  }
  return rc;
}

static int eq2_set_offload_blob(struct dsp_module* module,
                                uint32_t pipeline_id,
                                uint32_t comp_id) {
  char* mixer_name = eq2_blob_control_name(pipeline_id, comp_id);
  if (!mixer_name) {
    syslog(LOG_ERR, "eq2_set_offload_blob: Error creating mixer name");
    return -ENOMEM;
  }

  int rc = module_set_offload_blob(module, mixer_name);
  free(mixer_name);
  if (rc) {
    syslog(LOG_ERR, "eq2_set_offload_blob: Error setting offload blob");
  }
  return rc;
}

// The config blob to set bypass mode for SOF-backed DSP EQ.
static const uint8_t eq_iir_bypass_blob[] = {
    0x58, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xff, 0xff, 0xff, 0xff, 0xff,
    0xff, 0xff, 0xff, 0x01, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x9e,
    0x73, 0x13, 0x20, 0x00, 0x00, 0x00, 0x00, 0xb2, 0x7f, 0x00, 0x00};
static const size_t eq_iir_bypass_blob_size = 88;

/* There is no individual enable control for SOF-backed DSP EQ.
 * To enable, no ops is needed (the offload blob is applied as soon as it has
 * been configured). To disable, the known config blob for bypass mode will be
 * set instead.
 */
static int eq2_set_offload_mode(bool enabled,
                                uint32_t pipeline_id,
                                uint32_t comp_id) {
  if (enabled) {
    return 0;
  }

  char* mixer_name = eq2_blob_control_name(pipeline_id, comp_id);
  if (!mixer_name) {
    syslog(LOG_ERR, "eq2_set_offload_mode: Error creating mixer name");
    return -ENOMEM;
  }

  int rc = cras_alsa_config_set_tlv_bytes(mixer_name, eq_iir_bypass_blob,
                                          eq_iir_bypass_blob_size);
  free(mixer_name);
  if (rc) {
    syslog(LOG_ERR, "eq2_set_offload_mode: Failed to set blob for DSP offload");
  }
  return rc;
}

// Supported DSP module offload API sets.
static const struct dsp_module_offload_api module_offload_apis[] = {
    {
        .label = "drc",
        .probe = drc_probe,
        .set_offload_blob = drc_set_offload_blob,
        .set_offload_mode = drc_set_offload_mode,
    },
    {
        .label = "eq2",
        .probe = eq2_probe,
        .set_offload_blob = eq2_set_offload_blob,
        .set_offload_mode = eq2_set_offload_mode,
    },
};

static const struct dsp_module_offload_api* find_dsp_module_offload_api(
    const char* label) {
  if (!label) {
    return NULL;
  }

  for (int i = 0; i < ARRAY_SIZE(module_offload_apis); i++) {
    if (str_equals(module_offload_apis[i].label, label)) {
      return &module_offload_apis[i];
    }
  }
  return NULL;
}

typedef int (*exec_dsp_module_func_t)(const struct dsp_module_offload_api* api,
                                      uint32_t pipeline_id,
                                      uint32_t comp_id);

static int exec_probe(const struct dsp_module_offload_api* api,
                      uint32_t pipeline_id,
                      uint32_t comp_id) {
  return api->probe(pipeline_id, comp_id);
}

static int exec_enable(const struct dsp_module_offload_api* api,
                       uint32_t pipeline_id,
                       uint32_t comp_id) {
  return api->set_offload_mode(true, pipeline_id, comp_id);
}

static int exec_disable(const struct dsp_module_offload_api* api,
                        uint32_t pipeline_id,
                        uint32_t comp_id) {
  return api->set_offload_mode(false, pipeline_id, comp_id);
}

// Iterate DSP modules by scanning module labels through the pattern string of
// the given map, and execute the given function on each.
static int iterate_dsp_modules_from_offload_map(
    struct dsp_offload_map* offload_map,
    exec_dsp_module_func_t exec_func) {
  char* pattern = strndup(offload_map->dsp_pattern, DSP_PATTERN_MAX_SIZE - 1);

  // Scan through all DSP module labels from dsp_pattern
  char* p = strtok(pattern, ">");
  while (p) {
    const struct dsp_module_offload_api* module_offload_api =
        find_dsp_module_offload_api(p);
    if (!module_offload_api) {
      free(pattern);
      return -EINVAL;
    }

    int rc = exec_func(module_offload_api, offload_map->pipeline_id, 0);
    if (rc) {
      free(pattern);
      return rc;
    }

    p = strtok(NULL, ">");
  }
  free(pattern);
  return 0;
}

static int mixer_controls_ready_for_offload_to_dsp(
    struct dsp_offload_map* offload_map) {
  return iterate_dsp_modules_from_offload_map(offload_map, exec_probe);
}

// Exposed function implementations

int cras_dsp_offload_create_map(struct dsp_offload_map** offload_map,
                                const struct cras_ionode* node) {
  // An example of dsp_offload_map_str from board config:
  //    "Speaker:(1,) Headphone:(6,eq2>drc) Line Out:(10,eq2)"
  //     ^~~~~~~  ^                ^~~~~~~
  //     Name     Pipeline ID      DSP Pattern (optional)
  const char* map_str = cras_system_get_dsp_offload_map_str();

  // strstr(A,B) returns the substring of A started from the first occurrence
  // of B, or NULL if B is not present in A.
  char* node_str = strstr(map_str, node->name);
  if (!node_str) {
    *offload_map = NULL;
    return 0;
  }

  // Use sscanf to identify the first regex string piece (%d,%s) and obtain
  // both values respectively.
  int pipeline_id = 0;
  char pattern[DSP_PATTERN_MAX_SIZE] = "";
  if (sscanf(node_str, "%*[^(](%d,%[^)])", &pipeline_id, pattern) == EOF) {
    syslog(LOG_ERR, "Failed to create dsp_offload_map. Invalid format.");
    return -EINVAL;
  }

  // The valid pipeline ID must be a positive integer, while 0 is returned when
  // the matched substring is not an integer.
  if (pipeline_id <= 0) {
    syslog(LOG_ERR, "Failed to create dsp_offload_map. Invalid pipeline ID");
    return -EINVAL;
  }

  struct dsp_offload_map* offload_pipe_map =
      (struct dsp_offload_map*)calloc(1, sizeof(*offload_pipe_map));
  if (!offload_pipe_map) {
    return -ENOMEM;
  }

  offload_pipe_map->pipeline_id = pipeline_id;
  size_t len = strnlen(pattern, DSP_PATTERN_MAX_SIZE);
  if (len == 0 || len == DSP_PATTERN_MAX_SIZE) {
    offload_pipe_map->dsp_pattern =
        strndup(DSP_PATTERN_OFFLOAD_DEFAULT, DSP_PATTERN_MAX_SIZE - 1);
  } else {
    offload_pipe_map->dsp_pattern = strndup(pattern, DSP_PATTERN_MAX_SIZE - 1);
  }

  // The validity check to confirm the presence of the associated mixer controls
  // for offload to DSP.
  int rc = mixer_controls_ready_for_offload_to_dsp(offload_pipe_map);
  if (rc) {
    cras_dsp_offload_free_map(offload_pipe_map);
    return rc;
  }

  // Set to the initial state of offload.
  offload_pipe_map->parent_dev = node->dev;
  offload_pipe_map->state = DSP_PROC_NOT_STARTED;
  *offload_map = offload_pipe_map;
  return 0;
}

bool cras_dsp_offload_is_already_applied(struct dsp_offload_map* offload_map) {
  if (!offload_map) {
    return false;
  }

  if (offload_map->state != DSP_PROC_ON_DSP) {
    return false;
  }

  if (!offload_map->parent_dev || !offload_map->parent_dev->active_node) {
    syslog(LOG_ERR,
           "cras_dsp_offload_is_already_applied: invalid dev or active_node");
    return false;
  }

  uint32_t active_node_idx = offload_map->parent_dev->active_node->idx;
  return offload_map->applied_node_idx == active_node_idx;
}

int cras_dsp_offload_config_module(struct dsp_offload_map* offload_map,
                                   struct dsp_module* mod,
                                   const char* label) {
  if (!offload_map || !mod || !label) {
    syslog(LOG_ERR, "cras_dsp_offload_config_module: invalid argument(s)");
    return -EINVAL;
  }

  const struct dsp_module_offload_api* module_offload_api =
      find_dsp_module_offload_api(label);
  if (!module_offload_api) {
    syslog(LOG_ERR,
           "cras_dsp_offload_config_module: No offload api for module: %s",
           label);
    return -EINVAL;
  }

  return module_offload_api->set_offload_blob(mod, offload_map->pipeline_id, 0);
}

int cras_dsp_offload_set_state(struct dsp_offload_map* offload_map,
                               bool enabled) {
  if (!offload_map) {
    syslog(LOG_ERR, "cras_dsp_offload_set_state: offload map is invalid");
    return -EINVAL;
  }

  if (enabled) {
    int rc = iterate_dsp_modules_from_offload_map(offload_map, exec_enable);
    if (rc) {
      syslog(LOG_ERR, "cras_dsp_offload_set_state: Failed to set enabled");
      return rc;
    }

    offload_map->state = DSP_PROC_ON_DSP;
    offload_map->applied_node_idx = offload_map->parent_dev->active_node->idx;
  } else {
    // Skip the disable process when the current state is not offloaded.
    if (offload_map->state == DSP_PROC_ON_CRAS) {
      return 0;
    }

    int rc = iterate_dsp_modules_from_offload_map(offload_map, exec_disable);
    if (rc) {
      syslog(LOG_ERR, "cras_dsp_offload_set_state: Failed to set disabled");
      return rc;
    }

    offload_map->state = DSP_PROC_ON_CRAS;
  }
  return 0;
}

void cras_dsp_offload_reset_map(struct dsp_offload_map* offload_map) {
  if (!offload_map) {
    return;
  }
  offload_map->state = DSP_PROC_NOT_STARTED;
  cras_dsp_offload_clear_disallow_bit(offload_map, DISALLOW_OFFLOAD_BY_PATTERN);
}

void cras_dsp_offload_free_map(struct dsp_offload_map* offload_map) {
  if (!offload_map) {
    return;
  }
  free(offload_map->dsp_pattern);
  free(offload_map);
}
