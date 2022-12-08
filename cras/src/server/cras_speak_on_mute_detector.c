/* Copyright 2022 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "cras_speak_on_mute_detector.h"
#include "cras_tm.h"
#include "cras_system_state.h"
#include "cras_observer.h"

#define FAKE_DETECTOR_NOTIFY_PERIOD_MS 10000

static struct cras_timer *timer = NULL;

static void fake_detector_callback(struct cras_timer *unused, void *data)
{
	cras_observer_notify_speak_on_mute_detected();

	// Schedule the next callback.
	struct cras_tm *tm = cras_system_state_get_tm();
	timer = cras_tm_create_timer(tm, FAKE_DETECTOR_NOTIFY_PERIOD_MS,
				     fake_detector_callback, NULL);
}

void cras_speak_on_mute_detector_start()
{
	if (timer) {
		// Already enabled.
		return;
	}
	struct cras_tm *tm = cras_system_state_get_tm();
	timer = cras_tm_create_timer(tm, FAKE_DETECTOR_NOTIFY_PERIOD_MS,
				     fake_detector_callback, NULL);
}

void cras_speak_on_mute_detector_stop()
{
	if (!timer) {
		// Already disabled.
		return;
	}
	struct cras_tm *tm = cras_system_state_get_tm();
	cras_tm_cancel_timer(tm, timer);
	timer = NULL;
}
