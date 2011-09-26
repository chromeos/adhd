/* Copyright (c) 2011 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
#if !defined(_GPIO_SWITCH_MONITOR_H_)
#define _GPIO_SWITCH_MONITOR_H_

void gpio_switch_monitor(const char *thread_name,
                         const char *device_name,
                         unsigned    switch_event,
                         const char *insert_command,
                         const char *remove_command);
#endif
