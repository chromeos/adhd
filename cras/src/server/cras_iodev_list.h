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

struct cras_iodev;
struct cras_iodev_info;
struct cras_rclient;
struct cras_rstream;
struct cras_audio_format;

/* Gets the iodev that should be used for a stream of given type.
 * Args:
 *    type - The type of stream to find the output for. (media, voice).
 *    direction - Playback or capture.
 * Returns:
 *    A pointer to the device to use, or NULL if none found.
 */
struct cras_iodev *cras_get_iodev_for_stream_type(uint32_t type,
						  uint32_t direction);

/* Adds an output to the output list.
 * Args:
 *    output - the otuput to add.
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

/* Gets a list of outputs. Callee must free the list when finished.
 * Args:
 *    list_out - This will be set to the malloc'd area containing the list of
 *        devices.
 * Returns:
 *    The number of devices on the list.
 */
int cras_iodev_list_get_outputs(struct cras_iodev_info **list_out);

/* Gets a list of inputs. Callee must free the list when finished.
 * Args:
 *    list_out - This will be set to the malloc'd area containing the list of
 *        devices.
 * Returns:
 *    The number of devices on the list.
 */
int cras_iodev_list_get_inputs(struct cras_iodev_info **list_out);

/* Attaches a stream from a device.
 * Args:
 *    iodev - The device to add the stream to.
 *    stream - The stream to add.
 */
int cras_iodev_attach_stream(struct cras_iodev *iodev,
			     struct cras_rstream *stream);

/* Detaches a stream from a device.
 * Args:
 *    iodev - The device to remove the stream from.
 *    stream - The stream to remove.
 */
int cras_iodev_detach_stream(struct cras_iodev *iodev,
			     struct cras_rstream *stream);

/* Sets up the iodev for the given format if possible.  If the iodev can't
 * handle the requested format, it will modify the fmt parameter to inform the
 * caller of the actual format.
 * Args:
 *    iodev - the iodev you want the format for.
 *    fmt - pass in the desired format, is filled with the actual
 *      format on return.
 */
int cras_iodev_set_format(struct cras_iodev *iodev,
			  struct cras_audio_format *fmt);

/* Moves all streams of type to a new device.
 * Args:
 *    type - The stream type to move.
 *    index - The index of the otuput to move to.
 */
int cras_iodev_move_stream_type(uint32_t type, uint32_t index);

#endif /* CRAS_IODEV_LIST_H_ */
