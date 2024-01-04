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

// Use cases corresponding to ALSA UCM verbs. Each iodev has one use case.
enum CRAS_USE_CASE {
  // Default case for regular streams.
  CRAS_USE_CASE_HIFI,
  // For streams with block size <= 480 frames (10ms at 48KHz).
  CRAS_USE_CASE_LOW_LATENCY,
  // For low latency streams requiring raw audio (no effect processing in DSP).
  CRAS_USE_CASE_LOW_LATENCY_RAW,
  CRAS_NUM_USE_CASES,
};

// NOTE: Updates UMA as well, change with caution
static inline const char* cras_use_case_str(enum CRAS_USE_CASE use_case) {
  // clang-format off
    switch (use_case) {
    ENUM_STR(CRAS_USE_CASE_HIFI)
    ENUM_STR(CRAS_USE_CASE_LOW_LATENCY)
    ENUM_STR(CRAS_USE_CASE_LOW_LATENCY_RAW)
    default:
        return "INVALID_USE_CASE";
    }
  // clang-format on
}

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
