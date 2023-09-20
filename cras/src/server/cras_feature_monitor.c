// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cras/src/server/cras_feature_monitor.h"

#include "cras/server/main_message.h"
#include "cras/src/server/cras_iodev_list.h"

static void handle_feature_changed() {
  // NC availability is controlled by feature flags that may change dynamically.
  // Notify Chrome to refetch the node list to propagate NC support status.
  // TODO(b/287567735): Remove after launch when removing the flag.
  cras_iodev_list_update_device_list();
  cras_iodev_list_notify_nodes_changed();
}

int cras_feature_monitor_init() {
  return cras_main_message_add_handler(CRAS_MAIN_FEATURE_CHANGED,
                                       handle_feature_changed, NULL);
}
