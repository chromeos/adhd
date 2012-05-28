/* Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef CRAS_IODEV_INFO_H_
#define CRAS_IODEV_INFO_H_

#include <stddef.h>

#define CRAS_IODEV_NAME_BUFFER_SIZE 64

/* Identifying information about an IO device.
 *    idx - iodev index.
 *    priority - Used when deciding what device to play to/capture from.  Higher
 *      is better.
 *    name - Name displayed to the user.
 */
struct cras_iodev_info {
	size_t idx;
	size_t priority;
	char name[CRAS_IODEV_NAME_BUFFER_SIZE];
};

#endif /* CRAS_IODEV_INFO_H_ */
