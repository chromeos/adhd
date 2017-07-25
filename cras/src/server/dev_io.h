/* Copyright 2017 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * `dev_io` Handles playback to and capture from open devices.  It runs only on
 * the audio thread.
 */

#ifndef DEV_IO_H_
#define DEV_IO_H_

#include "cras_iodev.h"
#include "cras_types.h"

/*
 * Open input/output devices.
 *    dev - The device.
 *    wake_ts - When callback is needed to avoid xrun.
 *    coarse_rate_adjust - Hack for when the sample rate needs heavy correction.
 *    input_streaming - For capture, has the input received samples?
 */
struct open_dev {
	struct cras_iodev *dev;
	struct timespec wake_ts;
	int coarse_rate_adjust;
	int input_streaming;
	struct open_dev *prev, *next;
};

/*
 * Fetches streams from each device in `odev_list`.
 *    odev_list - The list of open devices.
 */
void dev_io_playback_fetch(struct open_dev *odev_list);

#endif /* DEV_IO_H_ */
