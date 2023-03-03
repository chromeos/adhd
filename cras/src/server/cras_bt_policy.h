/* Copyright 2021 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef CRAS_SRC_SERVER_CRAS_BT_POLICY_H_
#define CRAS_SRC_SERVER_CRAS_BT_POLICY_H_

#include "cras/src/server/cras_bt_device.h"
#include "cras/src/server/cras_bt_io.h"
#include "cras/src/server/cras_iodev.h"

// All the reasons for when CRAS schedule a suspend to BT device.
enum cras_bt_policy_suspend_reason {
  A2DP_LONG_TX_FAILURE,
  A2DP_TX_FATAL_ERROR,
  CONN_WATCH_TIME_OUT,
  HFP_SCO_SOCKET_ERROR,
  HFP_AG_START_FAILURE,
  UNEXPECTED_PROFILE_DROP,
};

// Starts monitoring messages sent for BT audio policy functions below.
void cras_bt_policy_start();

// Stops monitoring messages sent for BT audio policy functions below.
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
 *  | bt_io_manager    +------------+                              |
 *  +--------------------------------------------------------------+
 */
int cras_bt_policy_switch_profile(struct bt_io_manager* mgr);

// Cleans up associated policy instnaces when bt_io_manager is removed.
void cras_bt_policy_remove_io_manager(struct bt_io_manager* mgr);

// Cleans up associated policy instances when device is removed.
void cras_bt_policy_remove_device(struct cras_bt_device* device);

/* Sends message to main thread for scheduling suspend the connection
 * of |device| after |msec| delay. */
int cras_bt_policy_schedule_suspend(
    struct cras_bt_device* device,
    unsigned int msec,
    enum cras_bt_policy_suspend_reason suspend_reason);

/* Sends message to main thread for cancling any scheduled suspension
 * of given |device|. */
int cras_bt_policy_cancel_suspend(struct cras_bt_device* device);

// Starts the connection watch flow in main thread.
int cras_bt_policy_start_connection_watch(struct cras_bt_device* device);

// Stops the connection watch flow in main thread.
int cras_bt_policy_stop_connection_watch(struct cras_bt_device* device);

#endif  // CRAS_SRC_SERVER_CRAS_BT_POLICY_H_
