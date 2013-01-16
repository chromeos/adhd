/* Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef CRAS_IODEV_INFO_H_
#define CRAS_IODEV_INFO_H_

#include <stddef.h>
#include <sys/time.h>

#define CRAS_IODEV_NAME_BUFFER_SIZE 64
#define CRAS_NODE_NAME_BUFFER_SIZE 64

/* Identifying information about an IO device.
 *    idx - iodev index.
 *    priority - Used when deciding what device to play to/capture from.  Higher
 *      is better.
 *    plugged - Set true if this device is known to be plugged in.
 *    plugged_time - If plugged is true, this is the time it was attached.
 *    name - Name displayed to the user.
 */
struct cras_iodev_info {
	size_t idx;
	size_t priority;
	int plugged;
	struct timeval plugged_time;
	char name[CRAS_IODEV_NAME_BUFFER_SIZE];
};

#endif /* CRAS_IODEV_INFO_H_ */
