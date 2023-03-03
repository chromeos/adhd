// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cras/src/server/speak_on_mute_detector.h"

#include <errno.h>
#include <stdint.h>
#include <time.h>

#include "cras_util.h"

int speak_on_mute_detector_init(
    struct speak_on_mute_detector* d,
    const struct speak_on_mute_detector_config* cfg) {
  if (cfg->detection_window_size > 63) {
    // Reject 64+ because we store only 64 activities in a uint64_t.
    // Reject 64 because (uint64_t)1 << 64 is undefined behavior.
    return -EINVAL;
  }
  if (cfg->detection_threshold > cfg->detection_window_size) {
    return -EINVAL;
  }

  d->cfg = *cfg;
  speak_on_mute_detector_reset(d);
  return 0;
}

void speak_on_mute_detector_reset(struct speak_on_mute_detector* d) {
  d->voice_activities = 0;
  d->silence_until.tv_sec = 0;
  d->silence_until.tv_nsec = 0;
}

bool speak_on_mute_detector_add_voice_activity_at(
    struct speak_on_mute_detector* d,
    bool detected,
    const struct timespec* when) {
  // Record the activity.
  d->voice_activities <<= 1;
  d->voice_activities |= detected;

  if (!detected) {
    return false;
  }

  uint64_t bitmask = 1;
  bitmask <<= d->cfg.detection_window_size;
  bitmask -= 1;
  if (__builtin_popcountll(d->voice_activities & bitmask) <
      d->cfg.detection_threshold) {
    // Not enough voice activities.
    return false;
  }

  if (timespec_after(&d->silence_until, when)) {
    // Rate limited.
    return false;
  }

  d->silence_until = *when;
  add_timespecs(&d->silence_until, &d->cfg.rate_limit_duration);
  return true;
}
