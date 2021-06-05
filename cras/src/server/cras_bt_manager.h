/* Copyright (c) 2013 The Chromium OS Authors. All rights reserved.
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

void cras_bt_start(DBusConnection *conn, unsigned profile_disable_mask);
void cras_bt_stop(DBusConnection *conn);

#endif /* CRAS_BT_MANAGER_H_ */
