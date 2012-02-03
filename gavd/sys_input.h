/* Copyright (c) 2011 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
#if !defined(_SYS_INPUT_H_)
#define _SYS_INPUT_H_

unsigned sys_input_get_switch_state(int       fd,     /* /dev/input/event fd. */
                                    unsigned  sw,     /* SW_xxx identifier    */
                                    unsigned *state); /* out: 0 -> off
                                                       *      1 -> on
                                                       */
char *sys_input_get_device_name(const char *path); /* caller: free() result */
#endif
