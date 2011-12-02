/* Copyright (c) 2011 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
#if !defined(_DBUS_CONNECTION_H_)
#define _DBUS_CONNECTION_H__

/* state = 0 -> jack unplugged
 * state = 1 -> jack plugged
 */
void dbus_connection_jack_state(const char *jack_name, unsigned state);
#endif
