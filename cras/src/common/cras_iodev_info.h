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
 *    name - Name displayed to the user.
 */
struct cras_iodev_info {
	size_t idx;
	char name[CRAS_IODEV_NAME_BUFFER_SIZE];
};

/* Identifying information about an ionode on an iodev.
 *    iodev_idx - Index of the device this node belongs.
 *    ionode_idx - Index of this node on the device.
 *    priority - Priority of this node. Higher is better.
 *    plugged - Set true if this node is known to be plugged in.
 *    plugged_time - If plugged is true, this is the time it was attached.
 *    selected - Set true if this node is selected by a client.
 *    active - If this is the node currently being used.
 *    name - Name displayed to the user.
 */
struct cras_ionode_info {
	size_t iodev_idx;
	size_t ionode_idx;
	size_t priority;
	int plugged;
	struct timeval plugged_time;
	int selected;
	int active;
	char name[CRAS_NODE_NAME_BUFFER_SIZE];
};

/* This is used in the cras_client_set_node_attr API.
 *    IONODE_ATTR_PLUGGED - set the node as plugged/unplugged.
 *    IONODE_ATTR_SELECTED - set the node as selected/unselected.
 */
enum ionode_attr {
	IONODE_ATTR_PLUGGED,
	IONODE_ATTR_SELECTED,
};

#endif /* CRAS_IODEV_INFO_H_ */
