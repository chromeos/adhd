// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cras/src/server/cras_speak_on_mute_detector.h"

void cras_speak_on_mute_detector_init() {}

void cras_speak_on_mute_detector_enable(bool enabled) {}

void cras_speak_on_mute_detector_streams_changed(
    struct cras_rstream* all_streams) {}

int cras_speak_on_mute_detector_add_voice_activity(bool detected) {
  return 0;
}
