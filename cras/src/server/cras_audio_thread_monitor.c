/* Copyright 2018 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <stdbool.h>
#include <syslog.h>

#include "cras/src/server/audio_thread.h"
#include "cras/src/server/cras_iodev_list.h"
#include "cras/src/server/cras_main_message.h"
#include "cras/src/server/cras_observer.h"
#include "cras/src/server/cras_system_state.h"
#include "cras_types.h"
#include "cras_util.h"

#define MIN_WAIT_SECOND 30
#define UNDERRUN_EVENT_RATE_LIMIT_SECONDS 10
#define SEVERE_UNDERRUN_EVENT_RATE_LIMIT_SECONDS 5

struct cras_audio_thread_event_message {
  struct cras_main_message header;
  enum CRAS_AUDIO_THREAD_EVENT_TYPE event_type;
};

static void take_snapshot(enum CRAS_AUDIO_THREAD_EVENT_TYPE event_type) {
  struct cras_audio_thread_snapshot snapshot = {};

  struct timespec now_time;
  clock_gettime(CLOCK_MONOTONIC_RAW, &now_time);
  snapshot.timestamp = now_time;
  snapshot.event_type = event_type;
  audio_thread_dump_thread_info(cras_iodev_list_get_audio_thread(),
                                &snapshot.audio_debug_info);
  cras_system_state_add_snapshot(&snapshot);
}

static void cras_audio_thread_event_message_init(
    struct cras_audio_thread_event_message* msg,
    enum CRAS_AUDIO_THREAD_EVENT_TYPE event_type) {
  msg->header.type = CRAS_MAIN_AUDIO_THREAD_EVENT;
  msg->header.length = sizeof(*msg);
  msg->event_type = event_type;
}

int cras_audio_thread_event_send(enum CRAS_AUDIO_THREAD_EVENT_TYPE event_type) {
  struct cras_audio_thread_event_message msg = CRAS_MAIN_MESSAGE_INIT;
  cras_audio_thread_event_message_init(&msg, event_type);
  return cras_main_message_send(&msg.header);
}

int cras_audio_thread_event_a2dp_overrun() {
  return cras_audio_thread_event_send(AUDIO_THREAD_EVENT_A2DP_OVERRUN);
}

int cras_audio_thread_event_a2dp_throttle() {
  return cras_audio_thread_event_send(AUDIO_THREAD_EVENT_A2DP_THROTTLE);
}

int cras_audio_thread_event_debug() {
  return cras_audio_thread_event_send(AUDIO_THREAD_EVENT_DEBUG);
}

int cras_audio_thread_event_busyloop() {
  return cras_audio_thread_event_send(AUDIO_THREAD_EVENT_BUSYLOOP);
}

int cras_audio_thread_event_underrun() {
  return cras_audio_thread_event_send(AUDIO_THREAD_EVENT_UNDERRUN);
}

int cras_audio_thread_event_severe_underrun() {
  return cras_audio_thread_event_send(AUDIO_THREAD_EVENT_SEVERE_UNDERRUN);
}

int cras_audio_thread_event_drop_samples() {
  return cras_audio_thread_event_send(AUDIO_THREAD_EVENT_DROP_SAMPLES);
}

int cras_audio_thread_event_dev_overrun() {
  return cras_audio_thread_event_send(AUDIO_THREAD_EVENT_DEV_OVERRUN);
}

static struct timespec last_event_snapshot_time[AUDIO_THREAD_EVENT_TYPE_COUNT];
static struct timespec last_underrun_time = {0, 0};
static struct timespec last_severe_underrun_time = {0, 0};

/*
 * Callback function for handling audio thread events in main thread:
 *
 * Snapshot:
 * which takes a snapshot of the audio thread and waits at least 30 seconds
 * for the same event type. Events with the same event type within 30 seconds
 * will be ignored by the handle function.
 *
 * Severe underrun:
 *   Send D-Bus notification SevereUnderrun, at a max rate of 1 per 5 seconds
 *
 * Underrun:
 *   Send D-Bus notification Underrun, at a max rate of 1 per 10 seconds
 */
static void handle_audio_thread_event_message(struct cras_main_message* msg,
                                              void* arg) {
  struct cras_audio_thread_event_message* audio_thread_msg =
      (struct cras_audio_thread_event_message*)msg;
  struct timespec now_time;

  /*
   * Skip invalid event types
   */
  if (audio_thread_msg->event_type >= AUDIO_THREAD_EVENT_TYPE_COUNT) {
    return;
  }

  struct timespec* last_snapshot_time =
      &last_event_snapshot_time[audio_thread_msg->event_type];

  clock_gettime(CLOCK_REALTIME, &now_time);

  /*
   * Wait at least 30 seconds for the same event type
   */
  struct timespec diff_time;
  subtract_timespecs(&now_time, last_snapshot_time, &diff_time);
  if (timespec_is_zero(last_snapshot_time) ||
      diff_time.tv_sec >= MIN_WAIT_SECOND) {
    take_snapshot(audio_thread_msg->event_type);
    *last_snapshot_time = now_time;
  }
  /*
   * Handle (severe) underrun events
   */
  if (audio_thread_msg->event_type == AUDIO_THREAD_EVENT_SEVERE_UNDERRUN) {
    subtract_timespecs(&now_time, &last_severe_underrun_time, &diff_time);
    if (timespec_is_zero(&last_severe_underrun_time) ||
        diff_time.tv_sec >= SEVERE_UNDERRUN_EVENT_RATE_LIMIT_SECONDS) {
      cras_observer_notify_severe_underrun();
      last_severe_underrun_time = now_time;
    }
  } else if (audio_thread_msg->event_type == AUDIO_THREAD_EVENT_UNDERRUN) {
    subtract_timespecs(&now_time, &last_underrun_time, &diff_time);
    if (timespec_is_zero(&last_underrun_time) ||
        diff_time.tv_sec >= UNDERRUN_EVENT_RATE_LIMIT_SECONDS) {
      cras_observer_notify_underrun();
      last_underrun_time = now_time;
    }
  }
}

int cras_audio_thread_monitor_init() {
  memset(last_event_snapshot_time, 0,
         sizeof(struct timespec) * AUDIO_THREAD_EVENT_TYPE_COUNT);
  return cras_main_message_add_handler(CRAS_MAIN_AUDIO_THREAD_EVENT,
                                       handle_audio_thread_event_message, NULL);
}
