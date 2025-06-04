// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cras/src/server/cras_feature_monitor.h"

#include "cras/server/main_message.h"
#include "cras/server/platform/features/features.h"
#include "cras/server/s2/s2.h"
#include "cras/src/server/cras_iodev_list.h"

static void handle_feature_changed() {
  // NC availability is controlled by feature flags that may change dynamically.
  // Notify Chrome to refetch the node list to propagate NC support status.
  // TODO(b/287567735): Remove after launch when removing the flag.
  cras_s2_set_ap_nc_featured_allowed(
      cras_feature_enabled(CrOSLateBootAudioAPNoiseCancellation));
  cras_s2_set_style_transfer_featured_allowed(
      cras_feature_enabled(CrOSLateBootAudioStyleTransfer));
  cras_s2_set_output_plugin_processor_enabled(
      cras_feature_enabled(CrOSLateBootCrasOutputPluginProcessor));
  cras_iodev_list_update_device_list();
  cras_iodev_list_notify_nodes_changed();
}

int cras_feature_monitor_init() {
  // S2 initializes the feature states to false regardless of what has been set
  // in features.inc, so we need a force update for default true features.
  handle_feature_changed();
  return cras_main_message_add_handler(CRAS_MAIN_FEATURE_CHANGED,
                                       handle_feature_changed, NULL);
}
