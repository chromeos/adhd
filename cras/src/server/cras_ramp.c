/* Copyright 2016 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "cras/src/server/cras_ramp.h"

#include <errno.h>
#include <stdlib.h>

/*
 * Struct to hold ramping information.
 */
struct cras_ramp {
  int active;
  // Number of frames that have passed after starting ramping.
  int ramped_frames;
  // The targeted number of frames for whole ramping duration.
  int duration_frames;
  // The scaler increment that should be added to scaler for
  // every frame.
  float increment;
  // The initial scaler.
  float start_scaler;
  float target;
  // Callback function to call after ramping is done.
  void (*cb)(void* data);
  // Data passed to cb.
  void* cb_data;
};

void cras_ramp_destroy(struct cras_ramp* ramp) {
  free(ramp);
}

struct cras_ramp* cras_ramp_create() {
  struct cras_ramp* ramp;
  ramp = (struct cras_ramp*)malloc(sizeof(*ramp));
  if (ramp == NULL) {
    return NULL;
  }
  cras_ramp_reset(ramp);
  return ramp;
}

int cras_ramp_reset(struct cras_ramp* ramp) {
  ramp->active = 0;
  ramp->ramped_frames = 0;
  ramp->duration_frames = 0;
  ramp->increment = 0;
  ramp->start_scaler = 1.0;
  ramp->target = 1.0;
  return 0;
}

int cras_ramp_start(struct cras_ramp* ramp,
                    int mute_ramp,
                    float from,
                    float to,
                    int duration_frames,
                    cras_ramp_cb cb,
                    void* cb_data) {
  struct cras_ramp_action action;

  if (!ramp) {
    return -EINVAL;
  }

  // if from == to == 0 means we want to mute for duration_frames
  if (from == to && from != 0) {
    return 0;
  }

  // Get current scaler position so it can serve as new start scaler.
  action = cras_ramp_get_current_action(ramp);
  if (action.type == CRAS_RAMP_ACTION_INVALID) {
    return -EINVAL;
  }

  /* Set initial scaler to current scaler so ramping up/down can be
   * smoothly switched. */
  ramp->active = 1;
  if (action.type == CRAS_RAMP_ACTION_NONE) {
    ramp->start_scaler = from;
  } else {
    /* If this a mute ramp, we want to match the previous multiplier
     * so that there is not a jump in the audio. Otherwise, we are
     * applying a volume ramp so we need to multiply |from| by the
     * previous scaler so that we can stack volume ramps. */
    ramp->start_scaler = action.scaler;
    if (!mute_ramp) {
      ramp->start_scaler *= from;
    }
  }
  ramp->increment = (to - ramp->start_scaler) / duration_frames;
  ramp->target = to;
  ramp->ramped_frames = 0;
  ramp->duration_frames = duration_frames;
  ramp->cb = cb;
  ramp->cb_data = cb_data;
  return 0;
}

struct cras_ramp_action cras_ramp_get_current_action(
    const struct cras_ramp* ramp) {
  struct cras_ramp_action action;

  if (ramp->ramped_frames < 0) {
    action.type = CRAS_RAMP_ACTION_INVALID;
    action.scaler = 1.0;
    action.increment = 0.0;
    action.target = 1.0;
  } else if (ramp->active) {
    action.type = CRAS_RAMP_ACTION_PARTIAL;
    action.scaler = ramp->start_scaler + ramp->ramped_frames * ramp->increment;
    action.increment = ramp->increment;
    action.target = ramp->target;
  } else {
    action.type = CRAS_RAMP_ACTION_NONE;
    action.scaler = 1.0;
    action.increment = 0.0;
    action.target = 1.0;
  }
  return action;
}

int cras_ramp_update_ramped_frames(struct cras_ramp* ramp, int num_frames) {
  if (!ramp->active) {
    return -EINVAL;
  }
  ramp->ramped_frames += num_frames;
  if (ramp->ramped_frames >= ramp->duration_frames) {
    ramp->active = 0;
    if (ramp->cb && ramp->cb_data) {
      ramp->cb(ramp->cb_data);
    }
  }
  return 0;
}
