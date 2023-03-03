/* Copyright 2022 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef CRAS_SRC_SERVER_CRAS_SR_BT_ADAPTERS_H_
#define CRAS_SRC_SERVER_CRAS_SR_BT_ADAPTERS_H_

#include "cras/src/server/cras_sr.h"
#include "cras/src/server/cras_sr_bt_util.h"

struct cras_iodev_sr_bt_adapter;

/* Creates an iodev_sr_bt_adapter instance.
 * The users of the adapter should handle the sr lifetime.
 * Args:
 *    iodev - the iodev as the underlying data source of the sr.
 *    sr - the sr instance.
 *
 * Returns:
 *    The created adapter instance.
 */
struct cras_iodev_sr_bt_adapter* cras_iodev_sr_bt_adapter_create(
    struct cras_iodev* iodev,
    struct cras_sr* sr);

/* Destroys the adapter instance.
 * Args:
 *    adapter - the adapter to be destroyed.
 */
void cras_iodev_sr_bt_adapter_destroy(struct cras_iodev_sr_bt_adapter* adapter);

/* Gets the number of frames queued in the buffer.
 * Args:
 *    adapter - the adapter instance.
 *    tstamp - The associated hardware time stamp.
 * Returns:
 *    Number of frames queued.
 */
int cras_iodev_sr_bt_adapter_frames_queued(
    struct cras_iodev_sr_bt_adapter* adapter,
    struct timespec* tstamp);

// Get the delay for input in frames.
int cras_iodev_sr_bt_adapter_delay_frames(
    struct cras_iodev_sr_bt_adapter* adapter);

/* Gets a buffer to read from.
 * Args:
 *    adapter - the adapter instance.
 *    area - the area that stores the readable data. This will be updated by the
 *      function.
 *    frames - number of requested frames to read. This number will be updated
 *      to the number of readable frames in the area.
 * Returns:
 *    0 on success. It always returns 0 currently.
 */
int cras_iodev_sr_bt_adapter_get_buffer(
    struct cras_iodev_sr_bt_adapter* adapter,
    struct cras_audio_area** area,
    unsigned* frames);

/* Marks the number of read frames in the buffer from get_buffer.
 * Args:
 *    adapter - the adapter instance.
 *    nread - the number of read frames.
 * Returns:
 *    0 on success. Negative error code on failure.
 */
int cras_iodev_sr_bt_adapter_put_buffer(
    struct cras_iodev_sr_bt_adapter* adapter,
    unsigned nread);

// Flushes all the buffers.
int cras_iodev_sr_bt_adapter_flush_buffer(
    struct cras_iodev_sr_bt_adapter* adapter);

#endif
