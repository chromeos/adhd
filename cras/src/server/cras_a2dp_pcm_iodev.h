/* Copyright 2021 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef CRAS_A2DP_PCM_IODEV_H_
#define CRAS_A2DP_PCM_IODEV_H_

/* Creates an a2dp pcm iodev. */
struct cras_iodev *a2dp_pcm_iodev_create();

/* Destroys an a2dp pcm iodev. */
void a2dp_pcm_iodev_destroy(struct cras_iodev *iodev);

#endif /* CRS_A2DP_PCM_IODEV_H_ */
