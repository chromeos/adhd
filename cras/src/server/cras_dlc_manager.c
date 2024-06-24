// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cras/src/server/cras_dlc_manager.h"

#include "cras/server/platform/dlc/dlc.h"
#include "cras/src/server/cras_server_metrics.h"

void cras_dlc_manager_init(struct CrasDlcDownloadConfig dl_cfg) {
  download_dlcs_until_installed_with_thread(
      dl_cfg, cras_server_metrics_dlc_install_retried_times_on_success);
}
