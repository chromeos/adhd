// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CRAS_SRC_SERVER_SPEAK_ON_MUTE_DETECTOR_H_
#define CRAS_SRC_SERVER_SPEAK_ON_MUTE_DETECTOR_H_

#include <stdbool.h>
#include <stdint.h>
#include <time.h>

#ifdef __cplusplus
extern "C" {
#endif

struct speak_on_mute_detector_config {
  // Emit a detection event if more than `detection_threshold` VAD flags were
  // present in the given window.
  int detection_window_size;
  int detection_threshold;

  // Rate limit. Notifications are filtered if the last detection was within
  // the rate limit duration.
  struct timespec rate_limit_duration;
};

// This struct is private. Do not manipulate it directly.
// It's declared in the header so the size is known.
struct speak_on_mute_detector {
  struct speak_on_mute_detector_config cfg;

  // Bitset of voice activities.
  // The least significant bit is the most recent.
  // 1 means voice detected; 0 means not detected.
  uint64_t voice_activities;

  // If a event is detected before this time, it is silenced.
  // Used for rate limiting.
  struct timespec silence_until;
};

// Initialize the speak on mute detector.
int speak_on_mute_detector_init(struct speak_on_mute_detector*,
                                const struct speak_on_mute_detector_config*);

// Reset state of the speak on mute detector.
void speak_on_mute_detector_reset(struct speak_on_mute_detector*);

// Add a VAD result to the speak on mute detector.
// Returns whether the user should be notified.
bool speak_on_mute_detector_add_voice_activity_at(
    struct speak_on_mute_detector*,
    bool detected,
    const struct timespec* when);

#ifdef __cplusplus
}
#endif

#endif
