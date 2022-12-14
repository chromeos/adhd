// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stdint.h>
#include <syslog.h>

#include "cras_speak_on_mute_detector.h"
#include "cras_observer.h"
#include "cras_tm.h"
#include "cras_string.h"
#include "cras_system_state.h"
#include "speak_on_mute_detector.h"
#include "cras_types.h"
#include "cras_main_thread_log.h"
#include "cras_observer.h"
#include "cras_main_message.h"
#include "cras_stream_apm.h"
#include "cras_rtc.h"

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
	struct cras_rstream *target_stream;

	// The effective target stream apm.
	// This should only be set by maybe_update_vad_target.
	struct cras_stream_apm *effective_target;

	struct cras_observer_client *observer_client;
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

static void handle_voice_activity(bool detected, struct timespec *when)
{
	if (!cras_system_get_capture_mute()) {
		return;
	}
	if (speak_on_mute_detector_add_voice_activity_at(&detector.impl,
							 detected, when)) {
		cras_observer_notify_speak_on_mute_detected();
	}
}

static void handle_speak_on_mute_message(struct cras_main_message *mmsg,
					 void *arg)
{
	struct cras_speak_on_mute_message *msg =
		(struct cras_speak_on_mute_message *)mmsg;
	handle_voice_activity(msg->detected, &msg->when);
}

static void maybe_update_vad_target()
{
	struct cras_stream_apm *new_vad_target = NULL;
	// new_vad_target NULL means to disable VAD.
	if (detector.enabled && cras_system_get_capture_mute() &&
	    detector.target_stream) {
		new_vad_target = detector.target_stream->stream_apm;
	}

	if (new_vad_target == detector.effective_target) {
		return;
	}

	uint32_t target_stream_id =
		new_vad_target ? detector.target_stream->stream_id : 0;
	MAINLOG(main_log, MAIN_THREAD_VAD_TARGET_CHANGED, target_stream_id, 0,
		0);

	detector.effective_target = new_vad_target;
	speak_on_mute_detector_reset(&detector.impl);
	cras_stream_apm_notify_vad_target_changed(new_vad_target);
}

static void handle_capture_mute_changed(void *context, int muted,
					int mute_locked)
{
	maybe_update_vad_target();
}

static const struct cras_observer_ops speak_on_mute_observer_ops = {
	.capture_mute_changed = handle_capture_mute_changed,
};

void cras_speak_on_mute_detector_init()
{
	// TODO(b:262404106): Fine tune speak on mute detection parameters.
	struct speak_on_mute_detector_config cfg = { .detection_threshold = 28,
						     .detection_window_size =
							     30,
						     .rate_limit_duration = {
							     .tv_sec = 1,
							     .tv_nsec = 0,
						     } };

	// Should never fail for static configuration.
	assert(speak_on_mute_detector_init(&detector.impl, &cfg) == 0);

	detector.enabled = false;
	detector.target_stream = NULL;
	detector.effective_target = NULL;

	int rc = cras_main_message_add_handler(
		CRAS_MAIN_SPEAK_ON_MUTE, handle_speak_on_mute_message, NULL);
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

void cras_speak_on_mute_detector_enable(bool enabled)
{
	detector.enabled = enabled;
	maybe_update_vad_target();
}

static struct cras_rstream *find_target_stream(struct cras_rstream *all_streams)
{
	// Pick the first RTC stream using APM.
	// TODO(b/255935803): Implement VAD target selection.
	struct cras_rstream *stream;
	DL_FOREACH (all_streams, stream) {
		// TODO(b/255935803): Select VAD target based on real RTC detector result.
		// cras_rtc_check_stream_config only checks for the client type and block size.
		if (!cras_rtc_check_stream_config(stream)) {
			continue;
		}
		if (stream->stream_apm) {
			return stream;
		}
	}
	return NULL;
}

void cras_speak_on_mute_detector_streams_changed(
	struct cras_rstream *all_streams)
{
	detector.target_stream = find_target_stream(all_streams);
	maybe_update_vad_target();
}

int cras_speak_on_mute_detector_add_voice_activity(bool detected)
{
	struct timespec now;
	clock_gettime(CLOCK_MONOTONIC_RAW, &now);
	struct cras_speak_on_mute_message msg = {
		.base = {
			.length = sizeof(msg),
			.type = CRAS_MAIN_SPEAK_ON_MUTE,
		},
		.detected = detected,
		.when = now,
	};
	return cras_main_message_send(&msg.base);
}
