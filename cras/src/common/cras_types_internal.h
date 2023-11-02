/* Copyright 2023 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef CRAS_SRC_COMMON_CRAS_TYPES_INTERNAL_H_
#define CRAS_SRC_COMMON_CRAS_TYPES_INTERNAL_H_

#include "cras_types.h"

#ifdef __cplusplus
extern "C" {
#endif

static inline const char* audio_thread_event_type_to_str(
    enum CRAS_AUDIO_THREAD_EVENT_TYPE event) {
  switch (event) {
    case AUDIO_THREAD_EVENT_A2DP_OVERRUN:
      return "a2dp overrun";
    case AUDIO_THREAD_EVENT_A2DP_THROTTLE:
      return "a2dp throttle";
    case AUDIO_THREAD_EVENT_BUSYLOOP:
      return "busyloop";
    case AUDIO_THREAD_EVENT_DEBUG:
      return "debug";
    case AUDIO_THREAD_EVENT_SEVERE_UNDERRUN:
      return "severe underrun";
    case AUDIO_THREAD_EVENT_UNDERRUN:
      return "underrun";
    case AUDIO_THREAD_EVENT_DROP_SAMPLES:
      return "drop samples";
    case AUDIO_THREAD_EVENT_DEV_OVERRUN:
      return "device overrun";
    case AUDIO_THREAD_EVENT_OFFSET_EXCEED_AVAILABLE:
      return "minimum offset exceed available buffer frames";
    case AUDIO_THREAD_EVENT_UNREASONABLE_AVAILABLE_FRAMES:
      return "obtained unreasonable available frame count";
    default:
      return "no such type";
  }
}

#ifdef __cplusplus
}  // extern "C"
#endif

#endif  // CRAS_SRC_COMMON_CRAS_TYPES_INTERNAL_H_
