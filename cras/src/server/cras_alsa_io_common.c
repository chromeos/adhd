/* Copyright 2023 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
#include "cras/src/server/cras_alsa_io_common.h"

#include <sys/time.h>
#include <syslog.h>

#include "cras/src/server/cras_alsa_ucm.h"
#include "cras/src/server/cras_server_metrics.h"
#include "cras/src/server/cras_system_state.h"
#include "third_party/utlist/utlist.h"

struct cras_ionode* first_plugged_node(struct cras_iodev* iodev) {
  struct cras_ionode* n;

  /* When this is called at iodev creation, none of the nodes
   * are selected. Just pick the first plugged one and let Chrome
   * choose it later. */
  DL_FOREACH (iodev->nodes, n) {
    if (n->plugged) {
      return n;
    }
  }
  return iodev->nodes;
}

/* Returns true if the corresponding node_info of the specified input node has
 * Noise Cancellation flag in audio_effect. */
static bool noise_cancellation_support_is_exposed(uint32_t dev_idx,
                                                  uint32_t node_idx) {
  const struct cras_ionode_info* nodes;
  int nnodes;
  int i;

  nnodes = cras_system_state_get_input_nodes(&nodes);

  for (i = 0; i < nnodes; i++) {
    if (nodes[i].iodev_idx == dev_idx && nodes[i].ionode_idx == node_idx) {
      return nodes[i].audio_effect & EFFECT_TYPE_NOISE_CANCELLATION;
    }
  }

  syslog(LOG_ERR, "Cannot find input ionode_info dev_idx:%u node_idx:%u",
         dev_idx, node_idx);
  return false;
}

int cras_alsa_common_configure_noise_cancellation(
    struct cras_iodev* iodev,
    struct cras_use_case_mgr* ucm) {
  if (iodev->active_node->nc_provider == CRAS_IONODE_NC_PROVIDER_DSP) {
    bool enable_noise_cancellation =
        cras_system_get_noise_cancellation_enabled();
    int rc = ucm_enable_node_noise_cancellation(ucm, iodev->active_node->name,
                                                enable_noise_cancellation);
    if (rc < 0) {
      return rc;
    }

    enum CRAS_NOISE_CANCELLATION_STATUS nc_status;
    if (!noise_cancellation_support_is_exposed(iodev->info.idx,
                                               iodev->active_node->idx)) {
      nc_status = CRAS_NOISE_CANCELLATION_BLOCKED;
    } else if (!enable_noise_cancellation) {
      nc_status = CRAS_NOISE_CANCELLATION_DISABLED;
    } else {
      nc_status = CRAS_NOISE_CANCELLATION_ENABLED;
    }

    cras_server_metrics_device_noise_cancellation_status(iodev, nc_status);
  }

  return 0;
}

enum CRAS_IONODE_NC_PROVIDER cras_alsa_common_get_nc_provider(
    struct cras_use_case_mgr* ucm,
    const char* node_name) {
  if (ucm && cras_system_get_dsp_noise_cancellation_supported() &&
      ucm_node_noise_cancellation_exists(ucm, node_name)) {
    return CRAS_IONODE_NC_PROVIDER_DSP;
  }
  if (cras_system_get_ap_noise_cancellation_supported()) {
    return CRAS_IONODE_NC_PROVIDER_AP;
  }
  return CRAS_IONODE_NC_PROVIDER_NONE;
}
