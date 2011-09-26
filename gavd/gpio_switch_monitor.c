
/* Copyright (c) 2011 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
#include <assert.h>
#include <dirent.h>
#include <fcntl.h>
#include <malloc.h>
#include <sys/stat.h>
#include <sys/select.h>
#include <unistd.h>

/* TODO(thutt): Try to exclude this module if input events are not used. */
#include "verbose.h"
#include "sys_input.h"
#include "thread_management.h"
#include "utils.h"
#include "gpio_switch_monitor.h"

static void gpio_switch_notify_state(const char *thread_name,
                                     unsigned    state, /* 0 -> remove,
                                                         * 1 -> insert
                                                         */
                                     const char *insert_command,
                                     const char *remove_command)
{
    verbose_log(5, LOG_INFO, "%s: %s state: '%u'", __FUNCTION__,
                thread_name, state);

    if (insert_command != NULL && remove_command != NULL) {
        const char *cmd = state ? insert_command
                                : remove_command;
        utils_execute_command(cmd);
    } else {
        assert(insert_command == NULL && remove_command == NULL);
    }
}

void gpio_switch_monitor(const char *thread_name,
                         const char *device_name,
                         unsigned    switch_event,
                         const char *insert_command,
                         const char *remove_command)
{
    char     *device;
    unsigned  current_state;
    int       fd;

    VERBOSE_FUNCTION_ENTER();

    device = sys_input_find_device_by_name(device_name);
    if (device != NULL) {
        fd = open(device, O_RDONLY);
        if (fd != -1 &&
            sys_input_get_switch_state(fd, switch_event, &current_state)) {
            unsigned last_state = !current_state;

            assert(current_state == 0 || current_state == 1);
            while (!thread_management.exit) {
                struct timeval timeout;

                if (current_state != last_state) {
                    last_state = current_state;
                    gpio_switch_notify_state(thread_name, current_state,
                                             insert_command, remove_command);
                }

                timeout.tv_sec  = 0;
                timeout.tv_usec = 500000;   /* 1/2 second. */
                select(fd + 1, NULL, NULL, NULL, &timeout);
                sys_input_get_switch_state(fd, switch_event, &current_state);
            }
            close(fd);
        } else {
            verbose_log(0, LOG_ERR, "%s: unable to find device for '%s'",
                        __FUNCTION__, device_name);
        }
        free(device);
    } else {
        verbose_log(0, LOG_ERR, "%s: unable to find device for '%s'",
                        __FUNCTION__, device_name);
    }

    VERBOSE_FUNCTION_EXIT();
}
