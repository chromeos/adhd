/* Copyright 2013 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef CRAS_BT_MANAGER_H_
#define CRAS_BT_MANAGER_H_

#include <dbus/dbus.h>

/*
 * Bitmask for CRAS supported BT profiles. Currently only used for disabling
 * selected profiles on cras_bt_start().
 */
#define CRAS_BT_PROFILE_MASK_HFP (1 << 0)
#define CRAS_BT_PROFILE_MASK_A2DP (1 << 1)

/*
 * Represents a Bluetooth stack interface that CRAS can run with.
 * Members:
 *    profile_disable_mask - Bitmap to configure if certain profiles should
 *        be disabled.
 *    conn - The dBus connection handle.
 *    start - Callback to start the BT stack.
 *    stop - Callback to stop the BT stack.
 */
struct bt_stack {
	unsigned profile_disable_mask;
	DBusConnection *conn;
	void (*start)(struct bt_stack *s);
	void (*stop)(struct bt_stack *s);
};

/* Starts the default bt_stack interface. */
void cras_bt_start(DBusConnection *conn, unsigned profile_disable_mask);

/* Stops the current bt_stack interface. */
void cras_bt_stop(DBusConnection *conn);

/* Switches the current running stack to target. */
void cras_bt_switch_stack(struct bt_stack *target);

/* Switches the current running stack to default. */
void cras_bt_switch_default_stack();

#endif /* CRAS_BT_MANAGER_H_ */
