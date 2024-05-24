/* Copyright 2023 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef CRAS_SRC_COMMON_CRAS_TYPES_INTERNAL_H_
#define CRAS_SRC_COMMON_CRAS_TYPES_INTERNAL_H_

#include <stdio.h>

#include "cras/common/rust_common.h"
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

// The Bluetooth HFP telephony events happen in Floss.
enum CRAS_BT_HFP_TELEPHONY_EVENT {
  // Floss uhid device created (typically when SLC connects)
  CRAS_BT_HFP_TELEPHONY_EVENT_UHID_CREATE = 0,
  // Floss uhid device destroyed (typically when SLC disconnects)
  CRAS_BT_HFP_TELEPHONY_EVENT_UHID_DESTROY,
  // WebHID opens the uhid device
  CRAS_BT_HFP_TELEPHONY_EVENT_UHID_OPEN,
  // WebHID closes the uhid device
  CRAS_BT_HFP_TELEPHONY_EVENT_UHID_CLOSE,
  // WebHID sends incoming-call event
  CRAS_BT_HFP_TELEPHONY_EVENT_UHID_INCOMING_CALL,
  // WebHID sends off-hook=1 to answer an incoming call
  CRAS_BT_HFP_TELEPHONY_EVENT_UHID_ANSWER_CALL,
  // WebHID sends off-hook=0 to hang up current call
  CRAS_BT_HFP_TELEPHONY_EVENT_UHID_HANGUP_CALL,
  // WebHID sends off-hook=1 without prior incoming call
  CRAS_BT_HFP_TELEPHONY_EVENT_UHID_PLACE_ACTIVE_CALL,
  // WebHID sends phone-mute=1
  CRAS_BT_HFP_TELEPHONY_EVENT_UHID_MIC_MUTE,
  // WebHID sends phone-mute=0
  CRAS_BT_HFP_TELEPHONY_EVENT_UHID_MIC_UNMUTE,
  // Active call starts SCO alongside cras
  CRAS_BT_HFP_TELEPHONY_EVENT_CRAS_PLACE_ACTIVE_CALL,
  // Active call ends and SCO stops with cras
  CRAS_BT_HFP_TELEPHONY_EVENT_CRAS_REMOVE_ACTIVE_CALL,
  // Bluetooth headset sends ATA command to Floss
  CRAS_BT_HFP_TELEPHONY_EVENT_HF_ANSWER_CALL,
  // Bluetooth headset sends AT+CHUP command to Floss
  CRAS_BT_HFP_TELEPHONY_EVENT_HF_HANGUP_CALL,
  // Bluetooth headset sends AT+VGM=0
  CRAS_BT_HFP_TELEPHONY_EVENT_HF_MIC_MUTE,
  // Bluetooth headset sends AT+VGM=15
  CRAS_BT_HFP_TELEPHONY_EVENT_HF_MIC_UNMUTE,
  // Bluetooth headset queries current call list when SLC connected
  CRAS_BT_HFP_TELEPHONY_EVENT_HF_CURRENT_CALLS_QUERY,
};

static inline const char* cras_bt_hfp_telephony_event_to_str(
    enum CRAS_BT_HFP_TELEPHONY_EVENT event) {
  switch (event) {
    case CRAS_BT_HFP_TELEPHONY_EVENT_UHID_CREATE:
      return "UHID_CREATE";
    case CRAS_BT_HFP_TELEPHONY_EVENT_UHID_DESTROY:
      return "UHID_DESTROY";
    case CRAS_BT_HFP_TELEPHONY_EVENT_UHID_OPEN:
      return "UHID_OPEN";
    case CRAS_BT_HFP_TELEPHONY_EVENT_UHID_CLOSE:
      return "UHID_CLOSE";
    case CRAS_BT_HFP_TELEPHONY_EVENT_UHID_INCOMING_CALL:
      return "UHID_INCOMING_CALL";
    case CRAS_BT_HFP_TELEPHONY_EVENT_UHID_ANSWER_CALL:
      return "UHID_ANSWER_CALL";
    case CRAS_BT_HFP_TELEPHONY_EVENT_UHID_HANGUP_CALL:
      return "UHID_HANGUP_CALL";
    case CRAS_BT_HFP_TELEPHONY_EVENT_UHID_PLACE_ACTIVE_CALL:
      return "UHID_PLACE_ACTIVE_CALL";
    case CRAS_BT_HFP_TELEPHONY_EVENT_UHID_MIC_MUTE:
      return "UHID_MIC_MUTE";
    case CRAS_BT_HFP_TELEPHONY_EVENT_UHID_MIC_UNMUTE:
      return "UHID_MIC_UNMUTE";
    case CRAS_BT_HFP_TELEPHONY_EVENT_CRAS_PLACE_ACTIVE_CALL:
      return "CRAS_PLACE_ACTIVE_CALL";
    case CRAS_BT_HFP_TELEPHONY_EVENT_CRAS_REMOVE_ACTIVE_CALL:
      return "CRAS_REMOVE_ACTIVE_CALL";
    case CRAS_BT_HFP_TELEPHONY_EVENT_HF_ANSWER_CALL:
      return "HF_ANSWER_CALL";
    case CRAS_BT_HFP_TELEPHONY_EVENT_HF_HANGUP_CALL:
      return "HF_HANGUP_CALL";
    case CRAS_BT_HFP_TELEPHONY_EVENT_HF_MIC_MUTE:
      return "HF_MIC_MUTE";
    case CRAS_BT_HFP_TELEPHONY_EVENT_HF_MIC_UNMUTE:
      return "HF_MIC_UNMUTE";
    case CRAS_BT_HFP_TELEPHONY_EVENT_HF_CURRENT_CALLS_QUERY:
      return "HF_CURRENT_CALLS_QUERY";
    default:
      return "UNKNOWN_TELEPHONY_EVENT";
  }
}

// The call state in Floss.
enum CRAS_BT_HFP_CALL_STATE {
  CRAS_BT_HFP_CALL_IDLE = 0,
  CRAS_BT_HFP_CALL_INCOMING,
  CRAS_BT_HFP_CALL_DIALING,
  CRAS_BT_HFP_CALL_ALERTING,
  CRAS_BT_HFP_CALL_ACTIVE,
  CRAS_BT_HFP_CALL_HELD,
};

static inline const char* cras_bt_hfp_call_state_to_str(
    enum CRAS_BT_HFP_CALL_STATE state) {
  switch (state) {
    case CRAS_BT_HFP_CALL_IDLE:
      return "IDLE";
    case CRAS_BT_HFP_CALL_INCOMING:
      return "INCOMING";
    case CRAS_BT_HFP_CALL_DIALING:
      return "DIALING";
    case CRAS_BT_HFP_CALL_ALERTING:
      return "ALERTING";
    case CRAS_BT_HFP_CALL_ACTIVE:
      return "ACTIVE";
    case CRAS_BT_HFP_CALL_HELD:
      return "HELD";
    default:
      return "UNKNOWN_CALL_STATE";
  }
}

void print_cras_stream_active_ap_effects(FILE* f,
                                         CRAS_STREAM_ACTIVE_AP_EFFECT effects);

#ifdef __cplusplus
}  // extern "C"
#endif

#endif  // CRAS_SRC_COMMON_CRAS_TYPES_INTERNAL_H_
