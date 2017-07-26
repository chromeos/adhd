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

/*
 * Writes the samples fetched from the streams to the playback devices.
 *    odev_list - The list of open devices.  Devices will be removed when
 *                writing returns an error.
 */
int dev_io_playback_write(struct open_dev **odevs);

/* Only public for testing. */
int write_output_samples(struct open_dev **odevs,
			 struct open_dev *adev);

/*
 * Captures samples from each device in the list.
 *    list - Pointer to the list of input devices.  Devices that fail to read
 *           will be removed from the list.
 */
int dev_io_capture(struct open_dev **list);

/*
 * Send samples that have been captured to their streams.
 */
int dev_io_send_captured_samples(struct open_dev *idev_list);

/* Reads and/or writes audio samples from/to the devices. */
void dev_io_run(struct open_dev **odevs, struct open_dev **idevs);

/*
 * Removes a device from a list of devices.
 *    odev_list - A pointer to the list to modify.
 *    dev_to_rm - Find this device in the list and remove it.
 */
void dev_io_rm_open_dev(struct open_dev **odev_list,
			struct open_dev *dev_to_rm);

/* Returns a pointer to an open_dev if it is in the list, otherwise NULL. */
struct open_dev *dev_io_find_open_dev(struct open_dev *odev_list,
                                      const struct cras_iodev *dev);

/* Remove a stream from the provided list of devices. */
int dev_io_remove_stream(struct open_dev **dev_list,
			 struct cras_rstream *stream,
			 struct cras_iodev *dev);

#endif /* DEV_IO_H_ */
