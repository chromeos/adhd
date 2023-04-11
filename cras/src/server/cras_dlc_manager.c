// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cras/src/server/cras_dlc_manager.h"

#include <errno.h>
#include <syslog.h>

#include "cras/src/server/cras_system_state.h"
#include "cras/src/server/cras_tm.h"
#include "cras/src/server/rust/include/cras_dlc.h"

#define FIRST_TRY_MSEC 5000
#define RETRY_MSEC 60000
#define MAX_RETRY_COUNT 10

struct dlc_manager {
  struct cras_timer* retry_timer;
  int retry_counter;
};

static struct dlc_manager* dlc_manager = NULL;

// TODO(b/274547402): refine retry mechanism
static void download_supported_dlc(struct cras_timer* timer, void* arg) {
  struct cras_tm* tm = cras_system_state_get_tm();
  if (!tm) {
    syslog(LOG_ERR, "%s: failed to get cras timer", __func__);
    return;
  }

  if (!cras_dlc_is_available(CrasDlcNcAp)) {
    if (!cras_dlc_install(CrasDlcNcAp)) {
      syslog(LOG_ERR,
             "%s: unable to connect to dlcservice during `cras_dlc_install`.",
             __func__);
    }
    if (dlc_manager->retry_counter < MAX_RETRY_COUNT) {
      dlc_manager->retry_counter++;
      dlc_manager->retry_timer =
          cras_tm_create_timer(tm, RETRY_MSEC, download_supported_dlc, NULL);
      syslog(LOG_ERR, "%s: retry %d times", __func__,
             dlc_manager->retry_counter);
      return;
    } else {
      syslog(LOG_ERR,
             "%s: failed to install the DLC. Please check the network "
             "connection. Restart CRAS to retry.",
             __func__);
    }
  } else {
    syslog(LOG_INFO, "%s: successfully installed! Tried %d times.", __func__,
           dlc_manager->retry_counter);
  }

  // no more timer scheduled
  dlc_manager->retry_timer = NULL;
  cras_dlc_manager_destroy();
}

static void dlc_cancel_download() {
  if (!dlc_manager || !dlc_manager->retry_timer) {
    return;
  }

  struct cras_tm* tm = cras_system_state_get_tm();
  if (tm) {
    cras_tm_cancel_timer(tm, dlc_manager->retry_timer);
  }
  dlc_manager->retry_timer = NULL;
}

void cras_dlc_manager_init() {
  if (!dlc_manager) {
    struct dlc_manager* dm = (struct dlc_manager*)calloc(1, sizeof(*dm));
    if (!dm) {
      dlc_manager = NULL;
      return;
    }
    dlc_manager = dm;
  }

  struct cras_tm* tm = cras_system_state_get_tm();
  if (tm) {
    dlc_manager->retry_counter = 0;
    dlc_manager->retry_timer =
        cras_tm_create_timer(tm, FIRST_TRY_MSEC, download_supported_dlc, NULL);
  } else {
    syslog(LOG_ERR, "%s: failed to get cras timer", __func__);
  }
}

void cras_dlc_manager_destroy() {
  dlc_cancel_download();
  free(dlc_manager);
  dlc_manager = NULL;
}
