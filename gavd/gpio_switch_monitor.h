/* Copyright (c) 2011 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
#if !defined(_GPIO_SWITCH_MONITOR_H_)
#define _GPIO_SWITCH_MONITOR_H_

typedef unsigned (*switch_insert_command_fn_t)(void);
typedef unsigned (*switch_remove_command_fn_t)(void);

/* gpio_switch_monitor: Monitor events on a GPIO switch:
 *
 *   thread_name   : The name of the requesting thread.
 *                   This should be the thread descriptor name.
 *
 *   device_name   : The name of the 'event' information for the device.
 *                   See the definition of 'ADHD_INPUT_NAME_HEADPHONE_JACK'.
 *
 *   switch_event  : A /dev/input system switch event.
 *                   example: SW_HEADPHONE_INSERT, SW_MICROPHONE_INSERT
 *
 *   insert_command: A shell command line which is executed when the
 *                   switch is activated.
 *
 *   remove_command: A shell command line which is executed when the
 *                   switch is deactivated.
 */
void gpio_switch_monitor(const char                 *thread_name,
                         const char                 *jack,
                         const char                 *device_name,
                         unsigned                    switch_event,
                         switch_insert_command_fn_t  insert_command,
                         switch_remove_command_fn_t  remove_command);
#endif
