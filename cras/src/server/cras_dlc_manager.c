// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cras/src/server/cras_dlc_manager.h"

#include <syslog.h>

#include "cras/src/server/cras_server_metrics.h"
#include "cras/src/server/cras_system_state.h"
#include "cras/src/server/cras_tm.h"
#include "cras/src/server/rust/include/cras_dlc.h"

#define FIRST_TRY_MSEC 10000
#define MAX_RETRY_MSEC 1800000

struct dlc_download_context {
  enum CrasDlcId dlc_id;
  struct cras_timer* retry_timer;
  int retry_counter;
  unsigned int retry_ms;
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
 * Destroys the cras_dlc_manager if all the tasks have been `finished`.
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

  char dlc_id_string[CRAS_DLC_ID_STRING_MAX_LENGTH];
  cras_dlc_get_id_string(dlc_id_string, CRAS_DLC_ID_STRING_MAX_LENGTH,
                         context->dlc_id);

  bool dlc_available = cras_dlc_is_available(context->dlc_id);
  if (!dlc_available) {
    if (!cras_dlc_install(context->dlc_id)) {
      syslog(LOG_ERR,
             "%s: unable to connect to dlcservice during `cras_dlc_install`.",
             __func__);
    }
    ++context->retry_counter;
    context->retry_ms = MIN(context->retry_ms * 2, MAX_RETRY_MSEC);
    context->retry_timer = cras_tm_create_timer(
        tm, context->retry_ms, download_supported_dlc, context);
    syslog(LOG_WARNING, "%s: retry downloading `%s`, attempt #%d.", __func__,
           dlc_id_string, context->retry_counter);
    return;
  } else {
    syslog(LOG_DEBUG, "%s: successfully installed DLC of `%s`! Tried %d times.",
           __func__, dlc_id_string, context->retry_counter);
  }

  // No more timer scheduled. This is important to prevent from double freeing.
  context->retry_timer = NULL;
  ++dlc_manager->num_finished;
  cras_server_metrics_dlc_install_retried_times_on_success(
      context->dlc_id, context->retry_counter);

  // This could be dangerous if we export the `cras_dlc_manager_destroy`
  cras_dlc_manager_destroy_if_all_finished();
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
    for (int i = 0; i < NumCrasDlc; ++i) {
      dlc_manager->to_download[i].dlc_id = (enum CrasDlcId)i;
      dlc_manager->to_download[i].retry_counter = 0;
      dlc_manager->to_download[i].retry_ms = FIRST_TRY_MSEC;
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
