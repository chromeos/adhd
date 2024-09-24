// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cras/src/server/cras_dlc_manager.h"

#include "cras/server/main_message.h"
#include "cras/server/platform/dlc/dlc.h"
#include "cras/src/server/cras_iodev_list.h"
#include "cras/src/server/cras_server_metrics.h"

static int32_t dlc_install_on_success_callback(enum CrasDlcId dlc_id,
                                               int32_t elapsed_seconds) {
  const int ret = cras_server_metrics_dlc_install_elapsed_time_on_success(
      dlc_id, elapsed_seconds);

  struct cras_main_message msg = {
      .length = sizeof(msg),
      .type = CRAS_MAIN_DLC_INSTALLED,
  };
  cras_main_message_send(&msg);

  return ret;
}

static void notify_dlc_install_success() {
  cras_iodev_list_update_device_list();
  cras_iodev_list_notify_nodes_changed();
}

static int32_t dlc_install_on_failure_callback(enum CrasDlcId dlc_id,
                                               int32_t elapsed_seconds) {
  return cras_server_metrics_dlc_install_elapsed_time_on_failure(
      dlc_id, elapsed_seconds);
}

void cras_dlc_manager_init(struct CrasDlcDownloadConfig dl_cfg) {
  cras_main_message_add_handler(CRAS_MAIN_DLC_INSTALLED,
                                notify_dlc_install_success, NULL);

  download_dlcs_until_installed_with_thread(
      dl_cfg, dlc_install_on_success_callback, dlc_install_on_failure_callback);
}
