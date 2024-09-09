/* Copyright 2012 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/*
 * IO list manages the list of inputs and outputs available.
 */
#ifndef CRAS_SRC_SERVER_CRAS_IODEV_LIST_H_
#define CRAS_SRC_SERVER_CRAS_IODEV_LIST_H_

#include <stdbool.h>
#include <stdint.h>

#include "cras/common/rust_common.h"
#include "cras/src/server/cras_iodev.h"
#include "cras_types.h"

#ifdef __cplusplus
extern "C" {
#endif

struct cras_rclient;
struct stream_list;
struct cras_floop_pair;

// Device enabled/disabled callback.
typedef void (*device_enabled_callback_t)(struct cras_iodev* dev,
                                          void* cb_data);
typedef void (*device_disabled_callback_t)(struct cras_iodev* dev,
                                           void* cb_data);
typedef void (*device_removed_callback_t)(struct cras_iodev* dev);

// Initialize the list of iodevs.
void cras_iodev_list_init();

// Clean up any resources used by iodev.
void cras_iodev_list_deinit();

/* Adds an iodev to the respective direction list.
 * Args:
 *    iodev - the iodev to add.
 * Returns:
 *    0 on success, negative error on failure.
 */
int cras_iodev_list_add(struct cras_iodev* iodev);

/* Removes an output from the enabled list.
 * Args:
 *    dev - the iodev to remove.
 * Returns:
 *    0 on success, negative error on failure.
 */
int cras_iodev_list_rm(struct cras_iodev* dev);

/* Gets a list of outputs. Callee must free the list when finished.  If list_out
 * is NULL, this function can be used to return the number of outputs.
 * Args:
 *    list_out - This will be set to the malloc'd area containing the list of
 *        devices.  Ignored if NULL.
 * Returns:
 *    The number of devices on the list.
 */
int cras_iodev_list_get_outputs(struct cras_iodev_info** list_out);

/* Fills a list of DSP offload information.
 * Args:
 *    infos - This should be allocated by caller itself to fill info.
 *    max_num_info - The max number of info can be filled.
 * Returns:
 *    The number of DSP offload info filled in list, negative error on failure.
 */
int cras_iodev_list_fill_dsp_offload_infos(struct cras_dsp_offload_info* infos,
                                           uint32_t max_num_info);

/* Gets DSP offload state of an ionode.
 * Args:
 *    node_id - The id of the ionode.
 * Returns:
 *    The state in integer. DSP_PROC_UNSUPPORTED(-22) if offload is not
 *    supported.
 */
int cras_iodev_list_get_dsp_offload_state(cras_node_id_t node_id);

/* Gets a list of inputs. Callee must free the list when finished.  If list_out
 * is NULL, this function can be used to return the number of inputs.
 * Args:
 *    list_out - This will be set to the malloc'd area containing the list of
 *        devices.  Ignored if NULL.
 * Returns:
 *    The number of devices on the list.
 */
int cras_iodev_list_get_inputs(struct cras_iodev_info** list_out);

/* Returns the first enabled device.
 * Args:
 *    direction - Playback or capture.
 * Returns:
 *    Pointer to the first enabled device of direction.
 */
struct cras_iodev* cras_iodev_list_get_first_enabled_iodev(
    enum CRAS_STREAM_DIRECTION direction);

/* Returns SCO PCM device.
 * Args:
 *    direction - Playback or capture.
 * Returns:
 *    Pointer to the SCO PCM device of direction.
 */
struct cras_iodev* cras_iodev_list_get_sco_pcm_iodev(
    enum CRAS_STREAM_DIRECTION direction);

/* Returns the active node id.
 * Args:
 *    direction - Playback or capture.
 * Returns:
 *    The id of the active node.
 */
cras_node_id_t cras_iodev_list_get_active_node_id(
    enum CRAS_STREAM_DIRECTION direction);

/* Stores the following data to the shared memory server state region:
 * (1) device list
 * (2) node list
 * (3) selected nodes
 */
void cras_iodev_list_update_device_list();

// Stores the node list in the shared memory server state region.
void cras_iodev_list_update_node_list();

/* Gets the supported hotword models of an ionode. Caller should free
 * the returned string after use. */
char* cras_iodev_list_get_hotword_models(cras_node_id_t node_id);

// Sets the desired hotword model to an ionode.
int cras_iodev_list_set_hotword_model(cras_node_id_t id,
                                      const char* model_name);

// Notify that nodes are added/removed.
void cras_iodev_list_notify_nodes_changed();

/* Notify that active node is changed for the given direction.
 * Args:
 *    direction - Direction of the node.
 */
void cras_iodev_list_notify_active_node_changed(
    enum CRAS_STREAM_DIRECTION direction);

/* Sets an attribute of an ionode on a device.
 * Args:
 *    id - the id of the ionode.
 *    node_index - Index of the ionode on the device.
 *    attr - the attribute we want to change.
 *    value - the value we want to set.
 */
int cras_iodev_list_set_node_attr(cras_node_id_t id,
                                  enum ionode_attr attr,
                                  int value);

/*
 * Callback to notify any observing deivces of a display rotation change
 */
void cras_iodev_list_update_display_rotation();

/* Select a node as the preferred node.
 * Args:
 *    direction - Playback or capture.
 *    node_id - the id of the ionode to be selected. As a special case, if
 *        node_id is 0, don't select any node in this direction.
 */
void cras_iodev_list_select_node(enum CRAS_STREAM_DIRECTION direction,
                                 cras_node_id_t node_id);

/*
 * Checks if an iodev is enabled. By enabled we mean all default streams will
 * be routed to this iodev.
 */
int cras_iodev_list_dev_is_enabled(const struct cras_iodev* dev);

/*
 * Suspends the connection of all types of stream attached to given iodev.
 * This call doesn't disable the given iodev.
 */
void cras_iodev_list_suspend_dev(unsigned int dev_idx);

/*
 * Resumes the connection of all types of stream attached to given iodev.
 * This call doesn't enable the given dev.
 */
void cras_iodev_list_resume_dev(unsigned int dev_idx);

/*
 * Sets mute state to device of given index.
 * Args:
 *    dev_idx - Index of the device to set mute state.
 */
void cras_iodev_list_set_dev_mute(unsigned int dev_idx);

/*
 * Disables (if enabled) then closes the iodev group containing the given dev.
 * If no other iodev is enabled except the ones in the group, the fallback
 * device will be enabled before disabling the group.
 */
void cras_iodev_list_disable_and_close_dev_group(struct cras_iodev* dev);

/* Adds a node to the active devices list.
 * Args:
 *    direction - Playback or capture.
 *    node_id - The id of the ionode to be added.
 */
void cras_iodev_list_add_active_node(enum CRAS_STREAM_DIRECTION direction,
                                     cras_node_id_t node_id);

/* Removes a node from the active devices list.
 * Args:
 *    direction - Playback or capture.
 *    node_id - The id of the ionode to be removed.
 */
void cras_iodev_list_rm_active_node(enum CRAS_STREAM_DIRECTION direction,
                                    cras_node_id_t node_id);

// Returns 1 if the node is selected, 0 otherwise.
int cras_iodev_list_node_selected(struct cras_ionode* node);

// Notify the current volume of the given node.
void cras_iodev_list_notify_node_volume(struct cras_ionode* node);

/* Notify the current capture gain of the given node.
 * Args:
 *    node - The ionode whose gain shall be notified.
 *    gain - The gain of the ionode from [0, 100]
 */
void cras_iodev_list_notify_node_capture_gain(struct cras_ionode* node,
                                              int gain);

// Notify the current left right channel swapping state of the given node.
void cras_iodev_list_notify_node_left_right_swapped(struct cras_ionode* node);

// Handles the adding and removing of test iodevs.
void cras_iodev_list_add_test_dev(enum TEST_IODEV_TYPE type);

// Handles sending a command to a test iodev.
void cras_iodev_list_test_dev_command(unsigned int iodev_idx,
                                      enum CRAS_TEST_IODEV_CMD command,
                                      unsigned int data_len,
                                      const uint8_t* data);

// Gets the audio thread used by the devices.
struct audio_thread* cras_iodev_list_get_audio_thread();

// Gets the list of all active audio streams attached to devices.
struct stream_list* cras_iodev_list_get_stream_list();

// Sets the function to call when a device is enabled or disabled.
int cras_iodev_list_set_device_enabled_callback(
    device_enabled_callback_t enabled_cb,
    device_disabled_callback_t disabled_cb,
    device_removed_callback_t removed_cb,
    void* cb_data);

/* Registers loopback to an output device.
 * Args:
 *    loopback_type - Pre or post software DSP.
 *    output_dev_idx - Index of the target output device.
 *    hook_data - Callback function to process loopback data.
 *    hook_start - Callback for starting or stopping loopback.
 *    loopback_dev_idx - Index of the loopback device that
 *        listens for output data.
 */
void cras_iodev_list_register_loopback(enum CRAS_LOOPBACK_TYPE loopback_type,
                                       unsigned int output_dev_idx,
                                       loopback_hook_data_t hook_data,
                                       loopback_hook_control_t hook_start,
                                       unsigned int loopback_dev_idx);

/* Unregisters loopback from an output device by matching
 * loopback type and loopback device index.
 * Args:
 *    loopback_type - Pre or post software DSP.
 *    output_dev_idx - Index of the target output device.
 *    loopback_dev_idx - Index of the loopback device that
 *        listens for output data.
 */
void cras_iodev_list_unregister_loopback(enum CRAS_LOOPBACK_TYPE loopback_type,
                                         unsigned int output_dev_idx,
                                         unsigned int loopback_dev_idx);

// Suspends all hotwording streams.
int cras_iodev_list_suspend_hotword_streams();

// Resumes all hotwording streams.
int cras_iodev_list_resume_hotword_stream();

// Resolve a CRAS_NC_PROVIDER based on the iodev and user config.
// To be used in iodev open.
//
// iodev->active_node must exist.
CRAS_NC_PROVIDER cras_iodev_list_resolve_nc_provider(struct cras_iodev* iodev);

/* Sets the state of noise cancellation for input devices which supports noise
 * cancellation by suspend, enable/disable, then resume.
 */
void cras_iodev_list_reset_for_noise_cancellation();

/* Sets the state of style transfer for input devices. */
void cras_iodev_list_reset_for_style_transfer();

/* Reset the iodev so it's opened with low enough cb level for sidetone */
void cras_iodev_list_reset_for_sidetone();

// Sets dev_idx as the aec ref for a given stream.
int cras_iodev_list_set_aec_ref(unsigned int stream_id, unsigned int dev_idx);

/* Disconnects and reconnects all streams with apm on. Call this only in
 * main thread. */
void cras_iodev_list_reconnect_streams_with_apm();

// For unit test only.
void cras_iodev_list_reset();

/*
 * Converts input_node_gain [0, 100] to 100*dBFS.
 * Linear maps [0, 50) to [-2000, 0) and [50, 100] to [0, max_gain] 100*dBFS.
 * If it is an internal mic, it will query max_internal_mic_gain from board.ini
 * instead of using the default value 2000.
 */
long convert_dBFS_from_input_node_gain(long gain, bool is_internal_mic);

// The inverse function of convert_dBFS_from_input_node_gain.
long convert_input_node_gain_from_dBFS(long dBFS, bool is_internal_mic);

// Request the floop device with the given parameters
int cras_iodev_list_request_floop(const struct cras_floop_params* params);

/*
 * Callbacks for cras_floop_iodev.c to add/remove streams to/from the
 * floop odev
 */
void cras_iodev_list_enable_floop_pair(struct cras_floop_pair* pair);
void cras_iodev_list_disable_floop_pair(struct cras_floop_pair* pair);

/* Starts server stream for voice activity detection.
 * Args:
 *    dev_idx - Index of the input device to pin. Or NO_DEVICE to use the
 *              default device.
 */
void cras_iodev_list_create_server_vad_stream(int dev_idx);

/* Stops server stream for voice activity detection.
 * Args:
 *    dev_idx - Index of the input device.
 */
void cras_iodev_list_destroy_server_vad_stream(int dev_idx);

/* Returns true if stream is started by the system instead of by the user */
bool cras_iodev_list_is_utility_stream(const struct cras_rstream* stream);

/* Returns true if a given dev_idx is a floop device */
bool cras_iodev_list_is_floop_dev(int dev_idx);

#ifdef __cplusplus
}  // extern "C"
#endif

#endif  // CRAS_SRC_SERVER_CRAS_IODEV_LIST_H_
