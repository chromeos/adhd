/* Copyright 2021 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef CRAS_BT_POLICY_H_
#define CRAS_BT_POLICY_H_

/* Starts monitoring messages sent for BT audio policy functions below. */
void cras_bt_policy_start();

/* Stops monitoring messages sent for BT audio policy functions below. */
void cras_bt_policy_stop();

/* Sends message to main thread for switching associated bt iodevs
 * to use the active profile. This is achieved by close the iodevs,
 * update their active nodes, and then finally reopen them.
 *
 * This diagram describes how the profile switching happens. When
 * certain conditions met, bt iodev will call the APIs below to interact
 * with main thread to switch to another active profile.
 *
 * Audio thread:
 *  +--------------------------------------------------------------+
 *  | bt iodev                                                     |
 *  |              +------------------+    +-----------------+     |
 *  |              | condition met to |    | open, close, or |     |
 *  |           +--| change profile   |<---| append profile  |<--+ |
 *  |           |  +------------------+    +-----------------+   | |
 *  +-----------|------------------------------------------------|-+
 *              |                                                |
 * Main thread: |
 *  +-----------|------------------------------------------------|-+
 *  |           |                                                | |
 *  |           |      +------------+     +----------------+     | |
 *  |           +----->| set active |---->| switch profile |-----+ |
 *  |                  | profile    |     +----------------+       |
 *  | bt device        +------------+                              |
 *  +--------------------------------------------------------------+
 */
int cras_bt_policy_switch_profile(struct cras_bt_device *device,
				  struct cras_iodev *bt_iodev);

/* Cleans up associated policy instances when device is removed. */
void cras_bt_policy_remove_device(struct cras_bt_device *device);

#endif /* CRAS_BT_POLICY_H_ */
