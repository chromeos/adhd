// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cras/src/server/cras_dlc_manager.h"

#include <errno.h>
#include <syslog.h>

#include "cras/src/server/cras_server_metrics.h"
#include "cras/src/server/cras_system_state.h"
#include "cras/src/server/cras_tm.h"
#include "cras/src/server/rust/include/cras_dlc.h"

#define FIRST_TRY_MSEC 5000
#define RETRY_MSEC 60000
#define MAX_RETRY_COUNT 10

struct dlc_download_context {
  enum CrasDlcId dlc_id;
  struct cras_timer* retry_timer;
  int retry_counter;
};

struct dlc_manager {
  size_t num_finished;
  struct dlc_download_context to_download[NumCrasDlc];
};

static struct dlc_manager* dlc_manager = NULL;

/**
 * Cancels all the download tasks.
 */
static void dlc_cancel_download() {
  if (!dlc_manager) {
    return;
  }

  struct cras_tm* tm = cras_system_state_get_tm();
  if (!tm) {
    syslog(LOG_ERR, "%s: failed to get cras timer", __func__);
    return;
  }

  for (int i = 0; i < NumCrasDlc; ++i) {
    if (!dlc_manager->to_download[i].retry_timer) {
      continue;
    }
    cras_tm_cancel_timer(tm, dlc_manager->to_download[i].retry_timer);
    dlc_manager->to_download[i].retry_timer = NULL;
  }
}

void cras_dlc_manager_destroy() {
  dlc_cancel_download();
  free(dlc_manager);
  dlc_manager = NULL;
}

/**
 * Destroyes the cras_dlc_manager if all the tasks have been `finished`.
 * `finished` means either successfully installed or MAX_RETRY_COUNT reached.
 */
static void cras_dlc_manager_destroy_if_all_finished() {
  if (!dlc_manager) {
    return;
  }
  if (dlc_manager->num_finished < (size_t)NumCrasDlc) {
    return;
  }
  cras_dlc_manager_destroy();
}

// TODO(b/274547402): refine retry mechanism
static void download_supported_dlc(struct cras_timer* timer, void* arg) {
  struct dlc_download_context* context = (struct dlc_download_context*)arg;

  struct cras_tm* tm = cras_system_state_get_tm();
  if (!tm) {
    syslog(LOG_ERR, "%s: failed to get cras timer", __func__);
    return;
  }

  bool dlc_available = cras_dlc_is_available(context->dlc_id);
  if (!dlc_available) {
    if (!cras_dlc_install(context->dlc_id)) {
      syslog(LOG_ERR,
             "%s: unable to connect to dlcservice during `cras_dlc_install`.",
             __func__);
    }
    if (context->retry_counter < MAX_RETRY_COUNT) {
      ++context->retry_counter;
      context->retry_timer =
          cras_tm_create_timer(tm, RETRY_MSEC, download_supported_dlc, context);
      syslog(LOG_ERR, "%s: retry %d times", __func__, context->retry_counter);
      return;
    } else {
      syslog(LOG_ERR,
             "%s: failed to install the DLC. Please check the network "
             "connection. Restart CRAS to retry.",
             __func__);
    }
  } else {
    syslog(LOG_DEBUG, "%s: successfully installed! Tried %d times.", __func__,
           context->retry_counter);
  }

  // No more timer scheduled. This is important to prevent from double freeing.
  context->retry_timer = NULL;
  ++dlc_manager->num_finished;
  cras_server_metrics_dlc_manager_status(
      context->dlc_id, context->retry_counter,
      dlc_available ? CRAS_METRICS_DLC_STATUS_AVAILABLE
                    : CRAS_METRICS_DLC_STATUS_UNAVAILABLE);

  // This could be dangerous if we export the `cras_dlc_manager_destroy`
  cras_dlc_manager_destroy_if_all_finished();
}

void cras_dlc_manager_init() {
  if (!dlc_manager) {
    struct dlc_manager* dm =
        (struct dlc_manager*)calloc(1, sizeof(struct dlc_manager));
    if (!dm) {
      dlc_manager = NULL;
      return;
    }
    dlc_manager = dm;
  }

  struct cras_tm* tm = cras_system_state_get_tm();
  if (tm) {
    for (int i = 0; i < NumCrasDlc; ++i) {
      dlc_manager->to_download[i].dlc_id = (enum CrasDlcId)i;
      dlc_manager->to_download[i].retry_counter = 0;
      dlc_manager->to_download[i].retry_timer =
          cras_tm_create_timer(tm, FIRST_TRY_MSEC, download_supported_dlc,
                               &(dlc_manager->to_download[i]));
    }
  } else {
    syslog(LOG_ERR, "%s: failed to get cras timer", __func__);
  }
}

// cras_dlc_manager_test_only.h

bool cras_dlc_manager_is_null() {
  return dlc_manager == NULL;
}