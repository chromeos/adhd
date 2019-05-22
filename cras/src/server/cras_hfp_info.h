/* Copyright (c) 2013 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef CRAS_HFP_INFO_H_
#define CRAS_HFP_INFO_H_

#include "cras_iodev.h"
#include "cras_types.h"


/* Linked list to hold the information of callbacks to trigger
 * when the size of SCO packet has changed.
 */
struct hfp_packet_size_changed_callback {
	void *data;
	void (*cb)(void *data);
	struct hfp_packet_size_changed_callback *next, *prev;
};

/* Structure to handle sample transmission between CRAS and the SCO
 * socket acquired from bluez.
 */
struct hfp_info;

/* Creates an hfp_info instance.
 * Args:
 *    codec - 1 for CVSD, 2 for mSBC per HFP 1.7 specification.
 */
struct hfp_info *hfp_info_create(int codec);

/* Destroys given hfp_info instance. */
void hfp_info_destroy(struct hfp_info *info);

/* Checks if given hfp_info is running. */
int hfp_info_running(struct hfp_info *info);

/* Starts the hfp_info to transmit and reveice samples to and from the file
 * descriptor of a SCO socket. This should be called from main thread.
 */
int hfp_info_start(int fd, unsigned int mtu, struct hfp_info *info);

/* Stops given hfp_info. This implies sample transmission will
 * stop and socket be closed. This should be called from main thread.
 */
int hfp_info_stop(struct hfp_info *info);

/* Queries how many frames of data are queued.
 * Args:
 *    info - The hfp_info holding the buffer to query.
 *    dev - The iodev to indicate which buffer to query, playback
 *          or capture, depending on its direction.
 */
int hfp_buf_queued(struct hfp_info *info, const struct cras_iodev *dev);

/* Fill output buffer with zero frames.
 * Args:
 *    info - The hfp_info holding the output buffer.
 *    dev - The associated output device.
 *    nframes - How many zero frames to fill.
 * Returns:
 *    The actual number of zero frames filled.
 */
int hfp_fill_output_with_zeros(struct hfp_info *info,
			       struct cras_iodev *dev,
			       unsigned int nframes);

/* Force output buffer level to given value. Calling this may override
 * existing data so use it only when buffer has been filled by zeros.
 * Args:
 *    info - The hfp_info holding output buffer.
 *    dev - The associated output device.
 *    level - Value of the target output level.
 * Returns:
 *    0 for success, otherwise failure.
 */
int hfp_force_output_level(struct hfp_info *info,
			   struct cras_iodev *dev,
			   unsigned int level);

/* Gets how many bytes of the buffer are used.
 * Args:
 *    info - The hfp_info holding buffer.
 *    dev - The iodev which uses the buffer.
 */
int hfp_buf_size(struct hfp_info *info, struct cras_iodev *dev);

/* Acquire buffer of count frames for dev to write(or read,
 * depend on dev's direction).
 * Args:
 *    info - The hfp_info holding buffer.
 *    dev - The iodev to acquire buffer for.
 *    buf - To hold the returned pointer of acquired buffer.
 *    count - Number of bytes of buffer to acquire, will be filled with the
 *    actual acquired buffer size in bytes.
 */
void hfp_buf_acquire(struct hfp_info *info,  struct cras_iodev *dev,
		     uint8_t **buf, unsigned *count);

/* Releases the previously acquired buffer.
 * Args:
 *    info - The hfp_info holding the buffer.
 *    dev - The iodev who releases buffer.
 *    written_frames - The size of the previously acquired buffer in frames
 *    which's been used.
 */
void hfp_buf_release(struct hfp_info *info, struct cras_iodev *dev,
		     unsigned written_frames);

/* Adds cras_iodev to given hfp_info.  Only when an output iodev is added,
 * hfp_info starts sending samples to the SCO socket. Similarly, only when an
 * input iodev is added, it starts to read samples from SCO socket.
 */
int hfp_info_add_iodev(struct hfp_info *info, struct cras_iodev *dev);

/* Removes cras_iodev from hfp_info.  hfp_info will stop sending or
 * reading samples right after the iodev is removed. This function is used for
 * iodev closure.
 */
int hfp_info_rm_iodev(struct hfp_info *info, struct cras_iodev *dev);

/* Checks if there's any iodev added to the given hfp_info. */
int hfp_info_has_iodev(struct hfp_info *info);

#endif /* CRAS_HFP_INFO_H_ */
