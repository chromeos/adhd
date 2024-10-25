/* Copyright 2016 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef CRAS_SRC_SERVER_CRAS_OBSERVER_H_
#define CRAS_SRC_SERVER_CRAS_OBSERVER_H_

#include "cras/common/rust_common.h"
#include "cras/src/common/cras_observer_ops.h"

#ifdef __cplusplus
extern "C" {
#endif

struct cras_observer_client;

/* Add an observer.
 * Args:
 *    ops - Set callback function pointers in the operations that should be
 *          called for state changes, or NULL otherwise.
 *    context - Context pointer passed to the callbacks.
 * Returns:
 *    Valid pointer to the client reference, or NULL on memory allocation
 *    error.
 */
struct cras_observer_client* cras_observer_add(
    const struct cras_observer_ops* ops,
    void* context);

/* Retrieve the observed state changes.
 * Args:
 *    client - The client to query.
 *    ops - Filled with the current values in the callback table.
 */
void cras_observer_get_ops(const struct cras_observer_client* client,
                           struct cras_observer_ops* ops);

/* Update the observed state changes.
 * Args:
 *    client - The client to modify.
 *    ops - Set callback function pointers in the operations that should be
 *          called for state changes, or NULL otherwise.
 */
void cras_observer_set_ops(struct cras_observer_client* client,
                           const struct cras_observer_ops* ops);

// Returns non-zero if the given ops are empty.
int cras_observer_ops_are_empty(const struct cras_observer_ops* ops);

/* Remove this observer client.
 * Args:
 *    client - The client to remove.
 */
void cras_observer_remove(struct cras_observer_client* client);

// Initialize the observer server.
int cras_observer_server_init();

// Destroy the observer server.
void cras_observer_server_free();

// Notify observers of output volume change.
void cras_observer_notify_output_volume(int32_t volume);

// Notify observers of output mute change.
void cras_observer_notify_output_mute(int muted,
                                      int user_muted,
                                      int mute_locked);

// Notify observers of capture gain change.
void cras_observer_notify_capture_gain(int32_t gain);

// Notify observers of capture mute change.
void cras_observer_notify_capture_mute(int muted, int mute_locked);

// Notify observers of a nodes list change.
void cras_observer_notify_nodes(void);

// Notify observers of active output node change.
void cras_observer_notify_active_node(enum CRAS_STREAM_DIRECTION dir,
                                      cras_node_id_t node_id);

// Notify observers of output node volume change.
void cras_observer_notify_output_node_volume(cras_node_id_t node_id,
                                             int32_t volume);

// Notify observers of node left-right swap change.
void cras_observer_notify_node_left_right_swapped(cras_node_id_t node_id,
                                                  int swapped);

// Notify observers of input node gain change.
void cras_observer_notify_input_node_gain(cras_node_id_t node_id, int32_t gain);

// Notify observers of suspend state changed.
void cras_observer_notify_suspend_changed(int suspended);

// Notify observers of the number of active streams.
void cras_observer_notify_num_active_streams(enum CRAS_STREAM_DIRECTION dir,
                                             uint32_t num_active_streams);

// Notify observers of the number of non chrome input streams changed.
void cras_observer_notify_num_non_chrome_output_streams(
    uint32_t num_active_non_chrome_output_streams);

// Notify observers of the number of input streams with permission.
void cras_observer_notify_input_streams_with_permission(
    uint32_t num_input_streams[CRAS_NUM_CLIENT_TYPE]);

// Notify observers of the timestamp when hotword triggered.
void cras_observer_notify_hotword_triggered(int64_t tv_sec, int64_t tv_nsec);

// Notify observers the non-empty audio state changed.
void cras_observer_notify_non_empty_audio_state_changed(int active);

// Notify observers the bluetooth headset battery level changed.
void cras_observer_notify_bt_battery_changed(const char* address,
                                             uint32_t level);

// Notify observers of severe audio underrun
void cras_observer_notify_severe_underrun();

// Notify observers of audio underrun
void cras_observer_notify_underrun();

// Notify observers of a general survey trigger event
void cras_observer_notify_general_survey(enum CRAS_STREAM_TYPE stream_type,
                                         enum CRAS_CLIENT_TYPE client_type,
                                         const char* node_pair_type);

// Notify observers of a bluetooth audio survey trigger event
void cras_observer_notify_bluetooth_survey(uint32_t bt_flags);

// Notify observers of a output processing audio survey trigger event
void cras_observer_notify_output_proc_survey(enum CRAS_NODE_TYPE node_type);

// Notify observers of a speak on mute event
void cras_observer_notify_speak_on_mute_detected();

// Notify observers of a num stream ignore UI gains changed event
void cras_observer_notify_num_stream_ignore_ui_gains_changed(int num);

// Notify observers of the number of arc streams changed.
void cras_observer_notify_num_arc_streams(uint32_t num_arc_streams_changed);

// Notify observers that there is a report of ewma power.
void cras_observer_notify_ewma_power_reported(double power);

// Notify observers whether the current node supports sidetone.
void cras_observer_notify_sidetone_supported_changed(bool supported);

// Notify observers whether the audio effects are ready.
void cras_observer_notify_audio_effect_ui_appearance_changed(
    struct CrasEffectUIAppearance ui_appearance);

#ifdef __cplusplus
}  // extern "C"
#endif

#endif  // CRAS_SRC_SERVER_CRAS_OBSERVER_H_
