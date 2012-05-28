/* Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef CRAS_IODEV_INFO_H_
#define CRAS_IODEV_INFO_H_

#include <stddef.h>

#define CRAS_IODEV_NAME_BUFFER_SIZE 64

/* Identifying information about an IO device. */
struct cras_iodev_info {
	size_t idx;
	char name[CRAS_IODEV_NAME_BUFFER_SIZE];
};

#endif /* CRAS_IODEV_INFO_H_ */
