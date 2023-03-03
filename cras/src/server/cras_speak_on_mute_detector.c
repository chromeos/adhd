// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cras/src/server/cras_speak_on_mute_detector.h"

#include <stdint.h>
#include <syslog.h>

#include "cras/src/common/cras_string.h"
#include "cras/src/server/cras_iodev_list.h"
#include "cras/src/server/cras_main_message.h"
#include "cras/src/server/cras_main_thread_log.h"
#include "cras/src/server/cras_observer.h"
#include "cras/src/server/cras_rtc.h"
#include "cras/src/server/cras_stream_apm.h"
#include "cras/src/server/cras_system_state.h"
#include "cras/src/server/cras_tm.h"
#include "cras/src/server/server_stream.h"
#include "cras/src/server/speak_on_mute_detector.h"
#include "cras_types.h"

// Singleton.
static struct {
  struct speak_on_mute_detector impl;

  // State fields.
  // After changing these, call maybe_update_vad_target() to re-compute
  // the effective target and notify the audio thread.
  //
  // Whether speak on mute detection is enabled from the UI.
  bool enabled;
  // The target stream for VAD determined by the list of streams.
  // May not have a APM.
  struct cras_rstream* target_client_stream;
  // The currently active server VAD stream.
  struct cras_rstream* server_vad_stream;

  // The effective target stream apm.
  // This should only be set by maybe_update_vad_target.
  struct cras_stream_apm* effective_target;

  bool server_vad_stream_used;
  unsigned int server_vad_stream_pinned_dev_idx;

  struct cras_observer_client* observer_client;
} detector;

// Message send from the audio thread to the main thread.
// Only used to signal a voice activity result.
struct cras_speak_on_mute_message {
  struct cras_main_message base;

  // Voice activity detected.
  bool detected;
  // Timestamp of the detection.
  struct timespec when;
};

static void handle_voice_activity(bool detected, struct timespec* when) {
  if (!cras_system_get_capture_mute()) {
    return;
  }
  if (speak_on_mute_detector_add_voice_activity_at(&detector.impl, detected,
                                                   when)) {
    cras_observer_notify_speak_on_mute_detected();
  }
}

static void handle_speak_on_mute_message(struct cras_main_message* mmsg,
                                         void* arg) {
  struct cras_speak_on_mute_message* msg =
      (struct cras_speak_on_mute_message*)mmsg;
  handle_voice_activity(msg->detected, &msg->when);
}

// Destroy the server VAD stream if it is running.
static void maybe_destroy_server_vad_stream() {
  if (!detector.server_vad_stream_used) {
    return;
  }
  syslog(LOG_INFO, "destroying server vad stream with pinned_dev_idx = %d",
         detector.server_vad_stream_pinned_dev_idx);

  detector.server_vad_stream_used = false;
  detector.server_vad_stream_pinned_dev_idx = NO_DEVICE;
  cras_iodev_list_destroy_server_vad_stream(
      detector.server_vad_stream_pinned_dev_idx);
}

// Given the target client stream, enable or disable the server vad stream.
// Pass NULL to disable.
static void maybe_configure_server_vad_stream(
    struct cras_rstream* target_client_stream) {
  if (!target_client_stream) {
    // No target client.
    maybe_destroy_server_vad_stream();
    return;
  }
  if (target_client_stream->stream_apm) {
    // Client has APM. Use the client stream's APM.
    maybe_destroy_server_vad_stream();
    return;
  }
  // Client has no APM, otherwise.

  if (detector.server_vad_stream_used &&
      detector.server_vad_stream_pinned_dev_idx ==
          target_client_stream->pinned_dev_idx) {
    // The server vad stream matches the client configuration.
    return;
  }

  // Reconfigure server VAD stream.
  maybe_destroy_server_vad_stream();
  detector.server_vad_stream_used = true;
  detector.server_vad_stream_pinned_dev_idx =
      target_client_stream->pinned_dev_idx;
  syslog(LOG_INFO, "creating server vad stream with pinned_dev_idx = %d",
         detector.server_vad_stream_pinned_dev_idx);
  cras_iodev_list_create_server_vad_stream(
      detector.server_vad_stream_pinned_dev_idx);
}

bool should_run_vad() {
  return detector.enabled && cras_system_get_capture_mute();
}

static void maybe_update_vad_target() {
  // target_stream == NULL means to disable VAD.
  struct cras_rstream* target_stream = NULL;

  if (should_run_vad()) {
    if (detector.server_vad_stream) {
      // The existence of a server_vad_stream indicates that
      // the selected target_client_stream does not have a APM.
      target_stream = detector.server_vad_stream;
    } else if (detector.target_client_stream &&
               detector.target_client_stream->stream_apm) {
      target_stream = detector.target_client_stream;
    }
  }

  struct cras_stream_apm* new_vad_target =
      target_stream ? target_stream->stream_apm : NULL;

  if (new_vad_target == detector.effective_target) {
    return;
  }

  MAINLOG(
      main_log, MAIN_THREAD_VAD_TARGET_CHANGED,
      target_stream ? target_stream->stream_id : 0,
      detector.target_client_stream ? detector.target_client_stream->stream_id
                                    : 0,
      detector.server_vad_stream ? detector.server_vad_stream->stream_id : 0);

  detector.effective_target = new_vad_target;
  speak_on_mute_detector_reset(&detector.impl);
  cras_stream_apm_notify_vad_target_changed(new_vad_target);
}

// Callback to reflect external state changes:
// 1. The target client stream changes.
// 2. The enabled status of speak on mute detection changes.
// 3. The server vad stream becomes ready / removed.
static void handle_state_change() {
  maybe_update_vad_target();

  // Trigger the update of the server VAD stream to match
  // the target client + enabled status.
  // The updated is asynchronous and will generate an extra callback to
  // handle_state_change() again.
  maybe_configure_server_vad_stream(
      should_run_vad() ? detector.target_client_stream : NULL);
}

static void handle_capture_mute_changed(void* context,
                                        int muted,
                                        int mute_locked) {
  handle_state_change();
}

static const struct cras_observer_ops speak_on_mute_observer_ops = {
    .capture_mute_changed = handle_capture_mute_changed,
};

void cras_speak_on_mute_detector_init() {
  // TODO(b:262404106): Fine tune speak on mute detection parameters.
  struct speak_on_mute_detector_config cfg = {.detection_threshold = 28,
                                              .detection_window_size = 30,
                                              .rate_limit_duration = {
                                                  .tv_sec = 1,
                                                  .tv_nsec = 0,
                                              }};

  // Should never fail for static configuration.
  assert(speak_on_mute_detector_init(&detector.impl, &cfg) == 0);

  detector.enabled = false;
  detector.target_client_stream = NULL;
  detector.server_vad_stream = NULL;
  detector.server_vad_stream_used = false;
  detector.server_vad_stream_pinned_dev_idx = NO_DEVICE;
  detector.effective_target = NULL;

  int rc = cras_main_message_add_handler(CRAS_MAIN_SPEAK_ON_MUTE,
                                         handle_speak_on_mute_message, NULL);
  if (rc < 0) {
    syslog(LOG_ERR,
           "cannot add main message handler "
           "for cras speak on mute detector: %s",
           cras_strerror(-rc));
  }
  detector.observer_client =
      cras_observer_add(&speak_on_mute_observer_ops, NULL);
  if (detector.observer_client == NULL) {
    syslog(LOG_ERR, "cannot add observer client for speak on mute");
  }
}

void cras_speak_on_mute_detector_enable(bool enabled) {
  detector.enabled = enabled;
  handle_state_change();
}

// Return the client stream we should detect speak on mute behavior on.
static struct cras_rstream* find_target_client_stream(
    struct cras_rstream* all_streams) {
  // TODO(b/262518361): Select VAD target based on real RTC detector result.
  // cras_rtc_check_stream_config only checks for the client type and block
  // size.

  struct cras_rstream* stream = NULL;
  struct cras_rstream* first_rtc_stream = NULL;
  DL_FOREACH (all_streams, stream) {
    if (!cras_rtc_check_stream_config(stream)) {
      continue;
    }
    if (!first_rtc_stream) {
      first_rtc_stream = stream;
    }
    if (stream->stream_apm) {
      // Prefer RTC streams with a APM.
      return stream;
    }
  }

  // If no RTC streams have an APM, return the first RTC stream.
  return first_rtc_stream;
}

void cras_speak_on_mute_detector_streams_changed(
    struct cras_rstream* all_streams) {
  detector.target_client_stream = find_target_client_stream(all_streams);
  detector.server_vad_stream =
      server_stream_find_by_type(all_streams, SERVER_STREAM_VAD);

  syslog(
      LOG_DEBUG,
      "cras_speak_on_mute_detector_streams_changed: target_client_stream = "
      "0x%x; server_vad_stream = 0x%x",
      detector.target_client_stream ? detector.target_client_stream->stream_id
                                    : 0,
      detector.server_vad_stream ? detector.server_vad_stream->stream_id : 0);

  handle_state_change();
}

int cras_speak_on_mute_detector_add_voice_activity(bool detected) {
  struct timespec now;
  clock_gettime(CLOCK_MONOTONIC_RAW, &now);
  struct cras_speak_on_mute_message msg = {
      .base =
          {
              .length = sizeof(msg),
              .type = CRAS_MAIN_SPEAK_ON_MUTE,
          },
      .detected = detected,
      .when = now,
  };
  return cras_main_message_send(&msg.base);
}
