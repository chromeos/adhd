/* Copyright (c) 2011 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
#if !defined(_SYS_INPUT_H_)
#define _SYS_INPUT_H_

/* Caller must free() non-NULL results of sys_input_find_device_by_name() */
char *sys_input_find_device_by_name(const char *name);
unsigned sys_input_get_switch_state(int       fd,     /* /dev/input/event fd. */
                                    unsigned  sw,     /* SW_xxx identifier    */
                                    unsigned *state); /* out: 0 -> off
                                                       *      1 -> on
                                                       */
#endif
