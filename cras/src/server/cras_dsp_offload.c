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

#include "cras/src/server/cras_alsa_config.h"
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

/* Sets the offload mode to the coresponding module on DSP.
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
    syslog(LOG_ERR, "drc_probe: Error on probing blob control");
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
    syslog(LOG_ERR, "drc_probe: Error on probing enable control");
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
    syslog(LOG_ERR, "eq2_probe: Error on probing");
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
    size_t cmp_len = MAX(strlen(module_offload_apis[i].label), strlen(label));
    if (!strncmp(module_offload_apis[i].label, label, cmp_len)) {
      return &module_offload_apis[i];
    }
  }
  return NULL;
}

static int mixer_controls_ready_for_offload_to_dsp(
    struct dsp_offload_map* offload_map) {
  char* pattern = strdup(offload_map->dsp_pattern);

  // parse DSP module labels
  char* p = strtok(pattern, ">");
  while (p) {
    const struct dsp_module_offload_api* module_offload_api =
        find_dsp_module_offload_api(p);
    if (!module_offload_api) {
      free(pattern);
      return -EINVAL;
    }

    int rc = module_offload_api->probe(offload_map->pipeline_id, 0);
    if (rc) {
      free(pattern);
      return rc;
    }

    p = strtok(NULL, ">");
  }
  free(pattern);
  return 0;
}

// Exposed function implementations

int cras_dsp_offload_create_map(struct dsp_offload_map** offload_map,
                                enum CRAS_NODE_TYPE type) {
  struct dsp_offload_map* offload_pipe_map =
      (struct dsp_offload_map*)calloc(1, sizeof(*offload_pipe_map));
  if (!offload_pipe_map) {
    return -ENOMEM;
  }

  // TODO(b/188647460): obtain from the board config setting for the given type.
  offload_pipe_map->pipeline_id = 1;
  offload_pipe_map->dsp_pattern = "drc>eq2";

  // The validity check to confirm the presence of the associated mixer controls
  // for offload to DSP.
  int rc = mixer_controls_ready_for_offload_to_dsp(offload_pipe_map);
  if (rc) {
    free(offload_pipe_map);
    return rc;
  }

  *offload_map = offload_pipe_map;
  return 0;
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

int cras_dsp_offload_set_mode(struct dsp_offload_map* offload_map,
                              bool enabled) {
  if (!offload_map) {
    syslog(LOG_ERR, "cras_dsp_offload_set_mode: offload map is invalid");
    return -EINVAL;
  }

  char* pattern = strdup(offload_map->dsp_pattern);

  // Parse DSP module labels
  char* p = strtok(pattern, ">");
  while (p) {
    const struct dsp_module_offload_api* module_offload_api =
        find_dsp_module_offload_api(p);
    if (!module_offload_api) {
      syslog(LOG_ERR,
             "cras_dsp_offload_set_mode: No offload api for module: %s", p);
      free(pattern);
      return -EINVAL;
    }

    int rc = module_offload_api->set_offload_mode(enabled,
                                                  offload_map->pipeline_id, 0);
    if (rc) {
      syslog(LOG_ERR, "cras_dsp_offload_set_mode: Failed to set %s to %s",
             enabled ? "enabled" : "disabled", p);
      free(pattern);
      return rc;
    }

    p = strtok(NULL, ">");
  }

  free(pattern);
  return 0;
}