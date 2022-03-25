/* Copyright 2014 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef CRAS_BT_IODEV_H_
#define CRAS_BT_IODEV_H_

#include "cras_bt_device.h"
#include "cras_types.h"

struct cras_iodev;

/* TODO(jrwu) deprecate all usage of this in bt_io_manager. */
enum cras_bt_device_profile;

/*
 *    bt_iodevs - The input and output iodevs this |bt_io_manager| manages.
 *        These are actually wrappers to BT profile(A2DP and HFP) specific
 *        iodevs of the same direction. This allows |bt_io_manager| to control
 *        which BT profile to use at any scenario.
 *    active_profile - The flag to indicate the active BT profile, A2DP or HFP
 *        the underlying BT device is currently using. 0 means none of the BT
 *        profile has been added.
 */
struct bt_io_manager {
	struct cras_iodev *bt_iodevs[CRAS_NUM_DIRECTIONS];
	unsigned int active_profile;
	struct bt_io_manager *prev, *next;
};

/* Creates a bt_io_manager. */
struct bt_io_manager *bt_io_manager_create();

/* Destroys a bt_io_manager. */
void bt_io_manager_destroy(struct bt_io_manager *mgr);

/* Checks if bt_io_manager |target| is still alive. This is used to check
 * the validity of a bt_io_manager before accessing it in an async function
 * call.
 */
bool bt_io_manager_exists(struct bt_io_manager *target);

/* Appends |iodev| associated to |profile| to this bt_io_manager. */
void bt_io_manager_append_iodev(struct bt_io_manager *mgr,
				struct cras_iodev *iodev,
				enum cras_bt_device_profile profile,
				int software_volume_needed);

/* Removes the profile specific |iodev| from bt_io_manager.
 */
void bt_io_manager_remove_iodev(struct bt_io_manager *mgr,
				struct cras_iodev *iodev);

/*
 * Sets the cras_iodev's nodes under |mgr| to |plugged|. There might be
 * no need of this when BT stack can notify all profile connections in
 * one event.
 * Args:
 *     mgr - The bt_io_manager controlling BT iodevs.
 *     plugged - True means UI can select it and open it for streams.
 *         False to hide these nodes from UI, when device disconnects
 *         in progress.
 */
void bt_io_manager_set_nodes_plugged(struct bt_io_manager *mgr, int plugged);

/* Checks if |mgr| currently has A2DP iodev. Used for when there are
 * multiple BT device connections we need to decide which one to
 * expose to user/UI.
 */
int bt_io_manager_has_a2dp(struct bt_io_manager *mgr);

/* When AVRCP is connected set |mgr| to use hardware volume control. */
void bt_io_manager_set_use_hardware_volume(struct bt_io_manager *mgr,
					   int use_hardware_volume);

/* When remote BT device reports volume change through AVRCP, update
 * the volume value to |mgr|. */
void bt_io_manager_update_hardware_volume(struct bt_io_manager *mgr,
					  int volume);

#endif /* CRAS_BT_IODEV_H_ */
