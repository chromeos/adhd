
/* Copyright (c) 2011 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
#include <assert.h>
#include <dirent.h>
#include <fcntl.h>
#include <linux/input.h>
#include <malloc.h>
#include <sys/stat.h>
#include <unistd.h>

/* TODO(thutt): Try to exclude this module if input events are not used. */
#include "verbose.h"
#include "sys_input.h"
#include "thread_management.h"
#include "gpio_switch_monitor.h"

static void gpio_switch_notify_state(const char *thread_name, unsigned state)
{
    verbose_log(5, LOG_INFO, "%s: %s state: '%u'", __FUNCTION__,
                thread_name, state);
}

void gpio_switch_monitor(const char *thread_name,
                         const char *device_name,
                         unsigned    switch_event)
{
    char     *device;
    unsigned  state;
    int       fd;

    VERBOSE_FUNCTION_ENTER();

    device = sys_input_find_device_by_name(device_name);
    if (device != NULL) {
        fd = open(device, O_RDONLY);
        if (sys_input_get_switch_state(fd, switch_event, &state)) {
            gpio_switch_notify_state(thread_name, state);
        } else {
            verbose_log(5, LOG_INFO, "%s: %s switch state failed",
                        __FUNCTION__, thread_name);
        }
    } else {
        fd = -1;                /* Silence compiler warning. */
        verbose_log(5, LOG_ERR, "%s: unable to find device for '%s'",
                        __FUNCTION__, device_name);
    }

    while (!thread_management.exit) {
        sleep(1);
    }

    if (device != NULL) {
        close(fd);
        free(device);
    }
    VERBOSE_FUNCTION_EXIT();
}
