/* Copyright 2021 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef CRAS_SRC_SERVER_CRAS_FL_PCM_IODEV_H_
#define CRAS_SRC_SERVER_CRAS_FL_PCM_IODEV_H_

#include <stdint.h>

#include "cras_types.h"

struct cras_a2dp;
struct cras_hfp;

/* Creates an a2dp pcm iodev. Format bitmaps as defined in cras_fl_media.h
 * Args:
 *    a2dp - The associated cras_a2dp object.
 *    sample_rate - Bitmap of supported rates.
 *    bits_per_sample - Bitmap of supported sample sizes,
 *    channel_mode - Bitmap of supported channel modes.
 */
struct cras_iodev* a2dp_pcm_iodev_create(struct cras_a2dp* a2dp,
                                         int sample_rate,
                                         int bits_per_sample,
                                         int channel_mode);

// Destroys an a2dp pcm iodev.
void a2dp_pcm_iodev_destroy(struct cras_iodev* iodev);

/* Updates the audio delay by information from BT stack. This is supposed
 * to be used along with Floss API GetPresentationPosition.
 * Args:
 *    iodev - The a2dp_pcm iodev.
 *    total_bytes_read - The total number of bytes have been read by BT stack.
 *    remote_delay_report_ns - The AVDTP delay reporting from headset.
 *    data_position_ts - The timestamp of when BT stack read the last byte.
 */
void a2dp_pcm_update_bt_stack_delay(struct cras_iodev* iodev,
                                    uint64_t total_bytes_read,
                                    uint64_t remote_delay_report_ns,
                                    struct timespec* data_position_ts);

/* Creates an hfp pcm iodev.
 * Args:
 *    hfp - The associated cras_hfp object.
 *    dir - direction of the device.
 */
struct cras_iodev* hfp_pcm_iodev_create(struct cras_hfp* hfp,
                                        enum CRAS_STREAM_DIRECTION dir);

// Destroys an hfp pcm iodev.
void hfp_pcm_iodev_destroy(struct cras_iodev* iodev);

#endif  // CRAS_SRC_SERVER_CRAS_FL_PCM_IODEV_H_
