/* Copyright 2021 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef CRAS_A2DP_PCM_IODEV_H_
#define CRAS_A2DP_PCM_IODEV_H_

struct cras_a2dp;

/* Creates an a2dp pcm iodev. Format bitmaps as defined in cras_fl_media.h
 * Args:
 *    a2dp - The associated cras_a2dp object.
 *    sample_rate - Bitmap of supported rates.
 *    bits_per_sample - Bitmap of supported sample sizes,
 *    channel_mode - Bitmap of supported channel modes.
 */
struct cras_iodev *a2dp_pcm_iodev_create(struct cras_a2dp *a2dp,
					 int sample_rate, int bits_per_sample,
					 int channel_mode);

/* Destroys an a2dp pcm iodev. */
void a2dp_pcm_iodev_destroy(struct cras_iodev *iodev);

/* Update the volume of a2dp pcm iodev. */
void a2dp_pcm_update_volume(struct cras_iodev *iodev, unsigned int volume);

#endif /* CRS_A2DP_PCM_IODEV_H_ */
