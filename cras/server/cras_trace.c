// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cras/server/cras_trace.h"

PERCETTO_CATEGORY_DEFINE(CRAS_PERCETTO_CATEGORIES);

PERCETTO_TRACK_DEFINE(CRAS_SPK_HW_LEVEL, PERCETTO_TRACK_COUNTER);
PERCETTO_TRACK_DEFINE(CRAS_FLOOP_OUT_HW_LEVEL, PERCETTO_TRACK_COUNTER);
PERCETTO_TRACK_DEFINE(CRAS_INTERNAL_MIC_HW_LEVEL, PERCETTO_TRACK_COUNTER);
PERCETTO_TRACK_DEFINE(CRAS_FLOOP_IN_HW_LEVEL, PERCETTO_TRACK_COUNTER);

PERCETTO_TRACK_DEFINE(CRAS_SPK_WRITE_FRAMES, PERCETTO_TRACK_COUNTER);
PERCETTO_TRACK_DEFINE(CRAS_FLOOP_OUT_WRITE_FRAMES, PERCETTO_TRACK_COUNTER);
PERCETTO_TRACK_DEFINE(CRAS_FLOOP_IN_READ_FRAMES, PERCETTO_TRACK_COUNTER);
PERCETTO_TRACK_DEFINE(CRAS_INTERNAL_MIC_READ_FRAMES, PERCETTO_TRACK_COUNTER);

int cras_trace_init() {
  PERCETTO_INIT(PERCETTO_CLOCK_DONT_CARE);
  PERCETTO_REGISTER_TRACK(CRAS_SPK_HW_LEVEL);
  PERCETTO_REGISTER_TRACK(CRAS_FLOOP_OUT_HW_LEVEL);
  PERCETTO_REGISTER_TRACK(CRAS_INTERNAL_MIC_HW_LEVEL);
  PERCETTO_REGISTER_TRACK(CRAS_FLOOP_IN_HW_LEVEL);

  PERCETTO_REGISTER_TRACK(CRAS_SPK_WRITE_FRAMES);
  PERCETTO_REGISTER_TRACK(CRAS_FLOOP_OUT_WRITE_FRAMES);
  PERCETTO_REGISTER_TRACK(CRAS_FLOOP_IN_READ_FRAMES);
  PERCETTO_REGISTER_TRACK(CRAS_INTERNAL_MIC_READ_FRAMES);
  return 0;
}

void cras_trace_hw_level(enum CRAS_NODE_TYPE type, unsigned int hw_level) {
  switch (type) {
    case CRAS_NODE_TYPE_INTERNAL_SPEAKER:
      TRACE_COUNTER(audio, CRAS_SPK_HW_LEVEL, hw_level);
      break;
    case CRAS_NODE_TYPE_FLOOP:
      TRACE_COUNTER(audio, CRAS_FLOOP_IN_HW_LEVEL, hw_level);
      break;
    case CRAS_NODE_TYPE_FLOOP_INTERNAL:
      TRACE_COUNTER(audio, CRAS_FLOOP_OUT_HW_LEVEL, hw_level);
      break;
    case CRAS_NODE_TYPE_MIC:
      TRACE_COUNTER(audio, CRAS_INTERNAL_MIC_HW_LEVEL, hw_level);
      break;
    default:
      break;
  }
}

void cras_trace_frames(enum CRAS_NODE_TYPE type, unsigned int nframes) {
  switch (type) {
    case CRAS_NODE_TYPE_INTERNAL_SPEAKER:
      TRACE_COUNTER(audio, CRAS_SPK_WRITE_FRAMES, nframes);
      break;
    case CRAS_NODE_TYPE_FLOOP:
      TRACE_COUNTER(audio, CRAS_FLOOP_IN_READ_FRAMES, nframes);
      break;
    case CRAS_NODE_TYPE_MIC:
      TRACE_COUNTER(audio, CRAS_INTERNAL_MIC_READ_FRAMES, nframes);
      break;
    case CRAS_NODE_TYPE_FLOOP_INTERNAL:
      TRACE_COUNTER(audio, CRAS_FLOOP_OUT_WRITE_FRAMES, nframes);
      break;
    default:
      break;
  }
}

void cras_trace_underrun(enum CRAS_NODE_TYPE type,
                         enum CRAS_NODE_POSITION position) {
  char event[50];
  snprintf(event, 50, "%s_UNDERRUN", cras_node_type_to_str(type, position));
  TRACE_INSTANT(audio, event);
}

void cras_trace_overrun(enum CRAS_NODE_TYPE type,
                        enum CRAS_NODE_POSITION position) {
  char event[50];
  snprintf(event, 50, "%s_UNDERRUN", cras_node_type_to_str(type, position));
  TRACE_INSTANT(audio, event);
}
