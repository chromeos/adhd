// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CRAS_SRC_SERVER_CRAS_SPEAK_ON_MUTE_DETECTOR_H_
#define CRAS_SRC_SERVER_CRAS_SPEAK_ON_MUTE_DETECTOR_H_

#include <stdbool.h>

#include "cras/src/server/cras_rstream.h"
#include "cras/src/server/cras_stream_apm.h"

#ifdef __cplusplus
extern "C" {
#endif

// Initialize the speak on mute detector.
// Must be called from the main thread.
void cras_speak_on_mute_detector_init();

// Enable or disable the speak on mute detector.
// Must be called from the main thread.
void cras_speak_on_mute_detector_enable(bool enabled);

// Callback to update the voice activity detection target.
// Must be called from the main thread.
void cras_speak_on_mute_detector_streams_changed(
    struct cras_rstream* all_streams);

// Add a voice activity to the speak on mute detector.
// Must be called from the audio thread.
// Returns -errno on error.
int cras_speak_on_mute_detector_add_voice_activity(bool detected);

#ifdef __cplusplus
}
#endif

#endif
