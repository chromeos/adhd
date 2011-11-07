/* Copyright (c) 2011 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
#if !defined(_DBUS_TO_CHROME_FIFO_H_)
#define _DBUSFIFO_TO_CHROME_H_

#include "fifo.h"

FIFO_DECLARE(dbus_to_chrome_fifo);

/*  dbus_to_chrome_fifo_internal_speaker_headphone_state
 *   Indicate state change to internal speaker / heaphones.
 *
 *  state = 0 -> Speaker enable, headphone disabled.
 *  state = 1 -> Speaker disabled, headphone enabled.
 *  state not in {0, 1} -> error
 */
void  dbus_to_chrome_fifo_internal_speaker_headphone_state(unsigned state);
#endif
