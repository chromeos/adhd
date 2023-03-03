/* Copyright 2013 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef CRAS_SRC_SERVER_CRAS_SCO_H_
#define CRAS_SRC_SERVER_CRAS_SCO_H_

#include "cras/src/server/cras_bt_device.h"
#include "cras/src/server/cras_sr_bt_util.h"
#include "cras_audio_format.h"
#include "cras_types.h"

/* Linked list to hold the information of callbacks to trigger
 * when the size of SCO packet has changed.
 */
struct cras_sco_packet_size_changed_callback {
  void* data;
  void (*cb)(void* data);
  struct cras_sco_packet_size_changed_callback *next, *prev;
};

/* Structure to handle sample transmission between CRAS and the SCO
 * socket acquired from bluez.
 */
struct cras_sco;

/* Creates an cras_sco instance.
 *
 * Args:
 *    device - The associated bt device.
 */
struct cras_sco* cras_sco_create(struct cras_bt_device* device);

/* Enables the cras_sr model.
 * This function will try to init the related fields for cras_sr.
 *
 * Args:
 *    sco - The cras_sco instance.
 *    model - The type of the model.
 * Returns:
 *    0 on success. Otherwise, a negative error code is returned.
 */
int cras_sco_enable_cras_sr_bt(struct cras_sco* sco,
                               enum cras_sr_bt_model model);

/* Disables the cras_sr model.
 *
 * Args:
 *    sco - The cras_sco instance.
 */
void cras_sco_disable_cras_sr_bt(struct cras_sco* sco);

// Destroys given cras_sco instance.
void cras_sco_destroy(struct cras_sco* sco);

// Sets the wbs_logger to cras_sco instance.
void cras_sco_set_wbs_logger(struct cras_sco* sco,
                             struct packet_status_logger* wbs_logger);

// Sets the file descriptor to cras_sco.
int cras_sco_set_fd(struct cras_sco* sco, int fd);

// Gets the file descriptor of cras_sco.
int cras_sco_get_fd(struct cras_sco* sco);

// Closes the file descriptor of cras_sco.
int cras_sco_close_fd(struct cras_sco* sco);

// Checks if given cras_sco is running.
int cras_sco_running(struct cras_sco* sco);

/* Starts the cras_sco to transmit and receive samples to and from the file
 * descriptor of a SCO socket. This should be called from main thread.
 * Args:
 *    mtu - The packet size of HCI SCO packet.
 *    codec - 1 for CVSD, 2 for mSBC per HFP 1.7 specification.
 *    sco - The cras_sco instance.
 */
int cras_sco_start(unsigned int mtu, int codec, struct cras_sco* sco);

/* Stops given cras_sco. This implies sample transmission will
 * stop and socket be closed. This should be called from main thread.
 */
int cras_sco_stop(struct cras_sco* sco);

/* Queries how many frames of data are queued.
 * Args:
 *    sco - The cras_sco holding the buffer to query.
 *    direction - The direction to indicate which buffer to query, playback
 *          or capture.
 */
int cras_sco_buf_queued(struct cras_sco* sco,
                        enum CRAS_STREAM_DIRECTION direction);

/* Fill output buffer with zero frames.
 * Args:
 *    sco - The cras_sco holding the output buffer.
 *    nframes - How many zero frames to fill.
 * Returns:
 *    The actual number of zero frames filled.
 */
int cras_sco_fill_output_with_zeros(struct cras_sco* sco, unsigned int nframes);

/* Force output buffer level to given value. Calling this may override
 * existing data so use it only when buffer has been filled by zeros.
 * If no output device was added, calling this has no effect.
 * Args:
 *    sco - The cras_sco holding output buffer.
 *    level - Value of the target output level.
 */
void cras_sco_force_output_level(struct cras_sco* sco, unsigned int level);

/* Gets how many frames of the buffer are used.
 * Args:
 *    sco - The cras_sco holding buffer.
 *    direction - The direction of the buffer.
 */
int cras_sco_buf_size(struct cras_sco* sco,
                      enum CRAS_STREAM_DIRECTION direction);

/* Acquire buffer of count frames for dev to write(or read,
 * depend on dev's direction).
 * Args:
 *    sco - The cras_sco holding buffer.
 *    direction - The direction of dev to acquire buffer for.
 *    buf - To hold the returned pointer of acquired buffer.
 *    count - Number of bytes of buffer to acquire, will be filled with the
 *    actual acquired buffer size in bytes.
 */
void cras_sco_buf_acquire(struct cras_sco* sco,
                          enum CRAS_STREAM_DIRECTION direction,
                          uint8_t** buf,
                          unsigned* count);

/* Releases the previously acquired buffer.
 * Args:
 *    sco - The cras_sco holding the buffer.
 *    direction - The direction of dev to release buffer for.
 *    written_frames - The size of the previously acquired buffer in frames
 *    which's been used.
 */
void cras_sco_buf_release(struct cras_sco* sco,
                          enum CRAS_STREAM_DIRECTION direction,
                          unsigned written_frames);

/* Adds cras_iodev to given cras_sco.  Only when an output iodev is added,
 * cras_sco starts sending samples to the SCO socket. Similarly, only when an
 * input iodev is added, it starts to read samples from SCO socket.
 */
int cras_sco_add_iodev(struct cras_sco* sco,
                       enum CRAS_STREAM_DIRECTION direction,
                       struct cras_audio_format* format);

/* Removes cras_iodev from cras_sco.  cras_sco will stop sending or
 * reading samples right after the iodev is removed. This function is used for
 * iodev closure.
 */
int cras_sco_rm_iodev(struct cras_sco* sco,
                      enum CRAS_STREAM_DIRECTION direction);

// Checks if there's any iodev added to the given cras_sco.
int cras_sco_has_iodev(struct cras_sco* sco);

#endif  // CRAS_SRC_SERVER_CRAS_SCO_H_
