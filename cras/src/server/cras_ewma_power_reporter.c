// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#include "cras/src/server/cras_ewma_power_reporter.h"

#include <stdatomic.h>
#include <syslog.h>
#include <time.h>

#include "cras/include/cras_util.h"
#include "cras/server/main_message.h"
#include "cras/src/common/cras_string.h"
#include "cras/src/server/cras_observer.h"
#include "cras/src/server/cras_rtc.h"
#include "third_party/utlist/utlist.h"

static struct {
  atomic_bool enabled;
  atomic_uint_least32_t target_stream_id;

  double max_power;
  struct timespec next_ts;
} reporter;

// 100 ms interval
const struct timespec interval = {
    .tv_sec = 0,
    .tv_nsec = 100000000,
};

struct ewma_power_message {
  struct cras_main_message base;
  double power;
  struct timespec when;
};

static void handle_ewma_power_message(struct cras_main_message* mmsg,
                                      void* arg) {
  // struct ewma_power_message* msg = (struct ewma_power_message*)mmsg;
  // cras_observer_notify_ewma_power_reported(msg->power);
}

void cras_ewma_power_reporter_init() {
  int rc = cras_main_message_add_handler(CRAS_MAIN_EWMA_POWER_REPORT,
                                         handle_ewma_power_message, NULL);
  if (rc < 0) {
    syslog(LOG_ERR, "Cannot add main message handler for ewma power report: %s",
           cras_strerror(-rc));
  }

  atomic_store(&reporter.enabled, true);
  reporter.max_power = 0;
}

void cras_ewma_power_reporter_set_enabled(bool enabled) {
  atomic_store(&reporter.enabled, enabled);
}

void cras_ewma_power_reporter_set_target(uint32_t stream_id) {
  atomic_store(&reporter.target_stream_id, stream_id);
}

static int target_stream_score(const struct cras_rstream* stream) {
  if (stream->direction != CRAS_STREAM_INPUT) {
    return 0;
  }
  if (cras_rtc_check_stream_config(stream)) {
    return 110;
  }
  return 100;
}

void cras_ewma_power_reporter_streams_changed(
    struct cras_rstream* all_streams) {
  struct cras_rstream* stream = NULL;
  int best_score = 0;

  DL_FOREACH (all_streams, stream) {
    int score = target_stream_score(stream);
    if (score > best_score) {
      best_score = score;
      cras_ewma_power_reporter_set_target(stream->stream_id);
    }
  }
}

bool cras_ewma_power_reporter_should_calculate(const uint32_t stream_id) {
  if (!atomic_load(&reporter.enabled)) {
    return false;
  }
  if (atomic_load(&reporter.target_stream_id) != stream_id) {
    return false;
  }

  return true;
}

int cras_ewma_power_reporter_report(const uint32_t stream_id,
                                    const struct ewma_power* ewma) {
  struct timespec now;
  clock_gettime(CLOCK_MONOTONIC_RAW, &now);

  if (ewma->power > reporter.max_power) {
    reporter.max_power = ewma->power;
  }

  if (!timespec_after(&now, &reporter.next_ts)) {
    return 0;
  }

  struct ewma_power_message msg = {
      .base = {.length = sizeof(msg), .type = CRAS_MAIN_EWMA_POWER_REPORT},
      .power = reporter.max_power,
      .when = now,
  };

  add_timespecs(&now, &interval);
  reporter.next_ts = now;
  reporter.max_power = 0;
  return cras_main_message_send(&msg.base);
}
