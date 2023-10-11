/* Copyright 2018 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef CRAS_SRC_SERVER_CRAS_AUDIO_THREAD_MONITOR_H_
#define CRAS_SRC_SERVER_CRAS_AUDIO_THREAD_MONITOR_H_

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Notifies the main thread when A2DP buffer overruns.
 */
int cras_audio_thread_event_a2dp_overrun();

/*
 * Notifies the main thread when A2DP packet transmittion throttles.
 */
int cras_audio_thread_event_a2dp_throttle();

/*
 * Sends a debug event to the audio thread for debugging.
 */
int cras_audio_thread_event_debug();

/*
 * Notifies the main thread when a busyloop event happens.
 */
int cras_audio_thread_event_busyloop();

/*
 * Notifies the main thread when a underrun event happens.
 */
int cras_audio_thread_event_underrun();

/*
 * Notifies the main thread when a severe underrun event happens.
 */
int cras_audio_thread_event_severe_underrun();

/*
 * Notifies the main thread when a drop samples event happens.
 */
int cras_audio_thread_event_drop_samples();

/*
 * Notifies the main thread when a device overrun event happens.
 */
int cras_audio_thread_event_dev_overrun();

/*
 * Notifies the main thread when the minimum offset exceeds the available
 * frames.
 */
int cras_audio_thread_event_offset_exceed_available();

/*
 * Initializes audio thread monitor and sets main thread callback.
 */
int cras_audio_thread_monitor_init();

#ifdef __cplusplus
}  // extern "C"
#endif

#endif  // CRAS_SRC_SERVER_CRAS_AUDIO_THREAD_MONITOR_H_
