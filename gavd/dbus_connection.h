/* Copyright (c) 2011 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
#if !defined(_GAVD_DBUS_H_)
#define _GAVD_DBUS_H__

/* state = 0 -> headphone unplugged
 * state = 1 -> headphone plugged
 */
void dbus_connection_headphone_state(unsigned state);

/* state = 0 -> mircophone unplugged
 * state = 1 -> mircophone plugged
 */
void dbus_connection_microphone_state(unsigned state);

#endif
