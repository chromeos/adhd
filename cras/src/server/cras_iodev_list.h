/* Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/*
 * IO list manages the list of inputs and outputs available.
 */
#ifndef CRAS_IODEV_LIST_H_
#define CRAS_IODEV_LIST_H_

#include <stdint.h>

#include "cras_alert.h"
#include "cras_types.h"

struct cras_iodev;
struct cras_iodev_info;
struct cras_ionode;
struct cras_rclient;
struct cras_rstream;
struct cras_audio_format;

/* Initialize the list of iodevs. */
void cras_iodev_list_init();

/* Clean up any resources used by iodev. */
void cras_iodev_list_deinit();

/* Gets the iodev that should be used for a stream of given type.
 * Args:
 *    type - The type of stream to find the output for. (media, voice).
 *    direction - Playback or capture.
 *    idev - Filled with a pointer to the input device.
 *    odev - Filled with a pointer to the output device.
 * Returns:
 *    0 on success or a negative error on failure.
 */
int cras_get_iodev_for_stream_type(enum CRAS_STREAM_TYPE type,
				   enum CRAS_STREAM_DIRECTION direction,
				   struct cras_iodev **idev,
				   struct cras_iodev **odev);

/* Sets the device to default of its stream type.
 * Args:
 *     dir - The stream direction of default device.
 *     new_default - The device to set as default.
 * Returns:
 *     The pointer to previous default device, NULL if there was none.
 */
struct cras_iodev *cras_iodev_set_as_default(
		enum CRAS_STREAM_DIRECTION dir,
		struct cras_iodev *new_default);

/* Adds an output to the output list.
 * Args:
 *    output - the output to add.
 * Returns:
 *    0 on success, negative error on failure.
 */
int cras_iodev_list_add_output(struct cras_iodev *output);

/* Adds an input to the input list.
 * Args:
 *    input - the input to add.
 * Returns:
 *    0 on success, negative error on failure.
 */
int cras_iodev_list_add_input(struct cras_iodev *input);

/* Removes an output from the output list.
 * Args:
 *    output - the output to remove.
 * Returns:
 *    0 on success, negative error on failure.
 */
int cras_iodev_list_rm_output(struct cras_iodev *output);

/* Removes an input from the input list.
 * Args:
 *    output - the input to remove.
 * Returns:
 *    0 on success, negative error on failure.
 */
int cras_iodev_list_rm_input(struct cras_iodev *input);

/* Gets a list of outputs. Callee must free the list when finished.  If list_out
 * is NULL, this function can be used to return the number of outputs.
 * Args:
 *    list_out - This will be set to the malloc'd area containing the list of
 *        devices.  Ignored if NULL.
 * Returns:
 *    The number of devices on the list.
 */
int cras_iodev_list_get_outputs(struct cras_iodev_info **list_out);

/* Gets a list of inputs. Callee must free the list when finished.  If list_out
 * is NULL, this function can be used to return the number of inputs.
 * Args:
 *    list_out - This will be set to the malloc'd area containing the list of
 *        devices.  Ignored if NULL.
 * Returns:
 *    The number of devices on the list.
 */
int cras_iodev_list_get_inputs(struct cras_iodev_info **list_out);

/* Returns the active node id.
 * Args:
 *    direction - Playback or capture.
 * Returns:
 *    The id of the active node.
 */
cras_node_id_t cras_iodev_list_get_active_node_id(
	enum CRAS_STREAM_DIRECTION direction);

/* Moves all streams of type to a new device.
 * Args:
 *    type - The stream type to move.
 *    index - The index of the output to move to.
 */
int cras_iodev_move_stream_type(enum CRAS_STREAM_TYPE type, uint32_t index);

/* Moves all streams of type to the top priority device.  The top priority
 * device is the device that has had a jack plugged more recently or is last
 * attached to the system.
 * Args:
 *    type - The stream type to move.
 *    direction - Playback or capture.
 * Returns:
 *    0 on success or negative error code on failure.
 */
int cras_iodev_move_stream_type_top_prio(enum CRAS_STREAM_TYPE type,
					 enum CRAS_STREAM_DIRECTION direction);

/* Stores the following data to the shared memory server state region:
 * (1) device list
 * (2) node list
 * (3) selected nodes
 */
void cras_iodev_list_update_device_list();

/* Stores the node list in the shared memory server state region. */
void cras_iodev_list_update_node_list();

/* Adds a callback to call when the nodes are added/removed.
 * Args:
 *    cb - Function to call when there is a change.
 *    arg - Value to pass back to callback.
 */
int cras_iodev_list_register_nodes_changed_cb(cras_alert_cb cb, void *arg);

/* Removes a callback to call when the nodes are added/removed.
 * Args:
 *    cb - Function to call when there is a change.
 *    arg - Value to pass back to callback.
 */
int cras_iodev_list_remove_nodes_changed_cb(cras_alert_cb cb, void *arg);

/* Notify that nodes are added/removed. */
void cras_iodev_list_notify_nodes_changed();

/* Adds a callback to call when the active output/input node changes.
 * Args:
 *    cb - Function to call when there is a change.
 *    arg - Value to pass back to callback.
 */
int cras_iodev_list_register_active_node_changed_cb(cras_alert_cb cb,
						    void *arg);

/* Removes a callback to call when the active output/input node changes.
 * Args:
 *    cb - Function to call when there is a change.
 *    arg - Value to pass back to callback.
 */
int cras_iodev_list_remove_active_node_changed_cb(cras_alert_cb cb,
						  void *arg);

/* Notify that active output/input node is changed. */
void cras_iodev_list_notify_active_node_changed();

/* Gets the audio thread associated with an iodev.
 * Args:
 *    iodev - The iodev to check.
 * Returns:
 *    A pointer to the thread for this device or NULL if no thread is set.
 */
struct audio_thread *
cras_iodev_list_get_audio_thread(const struct cras_iodev *iodev);


/* Sets an attribute of an ionode on a device.
 * Args:
 *    id - the id of the ionode.
 *    node_index - Index of the ionode on the device.
 *    attr - the attribute we want to change.
 *    value - the value we want to set.
 */
int cras_iodev_list_set_node_attr(cras_node_id_t id,
				  enum ionode_attr attr, int value);

/* Select a node as the preferred node.
 * Args:
 *    direction - Playback or capture.
 *    node_id - the id of the ionode to be selected. As a special case, if
 *        node_id is 0, don't select any node in this direction.
 */
void cras_iodev_list_select_node(enum CRAS_STREAM_DIRECTION direction,
				 cras_node_id_t node_id);

/* Returns 1 if the node is selected, 0 otherwise. */
int cras_iodev_list_node_selected(struct cras_ionode *node);

#endif /* CRAS_IODEV_LIST_H_ */
