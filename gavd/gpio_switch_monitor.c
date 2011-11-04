
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
#include "fifo.h"
#include "gpio_switch_monitor.h"

typedef struct switch_state_t {
    const char *thread_name;
    const char *device_name;
    const char *insert_command;
    const char *remove_command;
    unsigned    state;          /* 0 -> remove, 1 -> insert */
} switch_state_t;

static const char *gpio_switch_decode_state(unsigned state)
{
    switch (state) {
    case 0:  return "off";
    case 1:  return "on";
    default: return "(invalid)";
    }
}

FIFO_ENTRY("GPIO Switch Notify State", gpio_switch_state,
{
    switch_state_t *ss = (switch_state_t *)data;

    VERBOSE_FUNCTION_ENTER("%s: %s", ss->thread_name,
                           gpio_switch_decode_state(ss->state));

    if (ss->insert_command != NULL && ss->remove_command != NULL) {
        const char *cmd = ss->state ? ss->insert_command
                                    : ss->remove_command;
        threads_lock_hardware();
        utils_execute_command(cmd);
        threads_unlock_hardware();
        free(ss);
    } else {
        /* If there is no command for insertion, or there is no
         * command for removal, then both commands must not exist.  In
         * other words, both must be present, or both must not be
         * present.
         *
         * This invariant is done so that board-based CPP identifiers
         * can be used to define the insert & remove commands.
         */
        assert(ss->insert_command == NULL && ss->remove_command == NULL);
    }

    VERBOSE_FUNCTION_EXIT("%s: %s", ss->thread_name,
                           gpio_switch_decode_state(ss->state));
});

static void gpio_switch_monitor_work(const char *thread_name,
                                     unsigned    switch_event,
                                     const char *insert_command,
                                     const char *remove_command,
                                     int         fd,
                                     unsigned    current_state)
{
    unsigned last_state = !current_state;

    assert(current_state == 0 || current_state == 1);
    while (!thread_management.tm_exit) {
        struct timeval timeout;

        verbose_log(9, LOG_INFO,
                    "%s: %s: last: '%s' current: '%s'",
                    __FUNCTION__, thread_name,
                    gpio_switch_decode_state(last_state),
                    gpio_switch_decode_state(current_state));
        if (current_state != last_state) {
            switch_state_t *ss = calloc((size_t)1,
                                        sizeof(switch_state_t));

            /* Only create workfifo entry if associated data can be
             * allocated.
             *
             * If 'ss' cannot be allocated, or if the work fifo item
             * cannot be added, don't update the state of the switch
             * either; try again during the next time quantum.
            */
            if (ss != NULL) {
                ss->thread_name    = thread_name;
                ss->insert_command = insert_command;
                ss->remove_command = remove_command;
                ss->state          = current_state;
                if (fifo_add_item(FIFO_ENTRY_ID(gpio_switch_state),
                                  ss)) {
                    last_state = current_state;
                } else {
                    free(ss);
                }
            }
        }

        timeout.tv_sec  = 0;
        timeout.tv_usec = 500000;   /* 1/2 second. */
        select(fd + 1, NULL, NULL, NULL, &timeout);
        sys_input_get_switch_state(fd, switch_event, &current_state);
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

    VERBOSE_FUNCTION_ENTER("%s, %s, %u, %s, %s",
                           thread_name, device_name, switch_event,
                           insert_command, remove_command);

    device = sys_input_find_device_by_name(device_name);
    if (device != NULL) {
        fd = open(device, O_RDONLY);
        if (fd != -1 &&
            sys_input_get_switch_state(fd, switch_event, &current_state)) {
            gpio_switch_monitor_work(thread_name,
                                     switch_event,
                                     insert_command,
                                     remove_command,
                                     fd,
                                     current_state);
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

    VERBOSE_FUNCTION_EXIT("%s, %s, %u, %s, %s",
                          thread_name, device_name, switch_event,
                          insert_command, remove_command);
}
