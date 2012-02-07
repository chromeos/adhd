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
#include <string.h>
#include <libudev.h>

#include "board.h"
#include "codec.h"
#include "verbose.h"
#include "sys_input.h"
#include "initialization.h"
#include "thread_management.h"
#include "utils.h"
#include "gpio_switch_monitor.h"

static char *microphone_jack_device;
static char *headphone_jack_device;

static void *gpio_headphone_monitor(void *arg)
{
    if (headphone_jack_device != NULL) {
        thread_descriptor_t *desc = (thread_descriptor_t *)arg;

        /* Initialization Code. */
        pthread_barrier_wait(&thread_management.tm_create_barrier);

        /* Wait for all other threads to start. */
        pthread_barrier_wait(&thread_management.tm_start_barrier);
        gpio_switch_monitor(desc->td_name,
                            "headphone",
                            headphone_jack_device,
                            SW_HEADPHONE_INSERT,
                            codec_headphone_insert,
                            codec_headphone_remove);
    }
    return NULL;
}


static void *gpio_microphone_monitor(void *arg)
{
    if (microphone_jack_device != NULL) {
        thread_descriptor_t *desc = (thread_descriptor_t *)arg;

        /* Initialization Code. */
        pthread_barrier_wait(&thread_management.tm_create_barrier);

        /* Wait for all other threads to start. */
        pthread_barrier_wait(&thread_management.tm_start_barrier);
        gpio_switch_monitor(desc->td_name,
                            "microphone",
                            microphone_jack_device,
                            SW_MICROPHONE_INSERT,
                            codec_microphone_insert,
                            codec_microphone_remove);
    }
    return NULL;
}

static unsigned is_microphone_jack(const char *name)
{
    regmatch_t  m[1];
    regex_t     regex;
    unsigned    success;
    const char *re = "^.*(Mic|Headset) Jack$"; /* Mic Jack    : wm8903, max98095
                                                * Headset Jack: max98095
                                                *   (for machines w/ one jack)
                                                */

    compile_regex(&regex, re);
    success = regexec(&regex, name, sizeof(m) / sizeof(m[0]), m, 0) == 0;
    regfree(&regex);
    return success;
}

static unsigned is_headphone_jack(const char *name)
{
    regmatch_t  m[1];
    regex_t     regex;
    unsigned    success;
    const char *re = "^.*(Headphone|Headset) Jack$"; /* Headphone: wm8903
                                                      * Headset  : max98095
                                                      */

    compile_regex(&regex, re);
    success = regexec(&regex, name, sizeof(m) / sizeof(m[0]), m, 0) == 0;
    regfree(&regex);
    return success;
}

static void enumerate_input(void)
{
    struct udev            *udev;
    struct udev_enumerate  *enumerate;
    struct udev_list_entry *dl;
    struct udev_list_entry *dev_list_entry;

    udev = udev_new();
    assert(udev != NULL);
    enumerate = udev_enumerate_new(udev);
    udev_enumerate_add_match_subsystem(enumerate, "input");
    udev_enumerate_scan_devices(enumerate);
    dl = udev_enumerate_get_list_entry(enumerate);

    udev_list_entry_foreach(dev_list_entry, dl) {
        const char         *path    = udev_list_entry_get_name(dev_list_entry);
        struct udev_device *dev     = udev_device_new_from_syspath(udev, path);
        const char         *devnode = udev_device_get_devnode(dev);

        if (devnode != NULL) {
            char *ioctl_name = sys_input_get_device_name(devnode);
            if (ioctl_name != NULL) {
                if (is_microphone_jack(ioctl_name)) {
                    microphone_jack_device = strdup(devnode);
                    verbose_log(5, LOG_INFO, "%s: microphone switch: %s",
                                __FUNCTION__, microphone_jack_device);
                }

                if (is_headphone_jack(ioctl_name)) {
                    headphone_jack_device = strdup(devnode);
                    verbose_log(5, LOG_INFO, "%s: headphone switch: %s",
                                __FUNCTION__, headphone_jack_device);
                }
                free(ioctl_name);
            }
        }
    }
    udev_enumerate_unref(enumerate);
    udev_unref(udev);
}

static void initialize(void)
{
    microphone_jack_device = NULL;
    headphone_jack_device  = NULL;
    enumerate_input();
}


static void finalize(void)
{
    free(microphone_jack_device);
    free(headphone_jack_device);
}


INITIALIZER("headphone / microphone jack monitor", initialize, finalize);
THREAD_DESCRIPTOR("Internal Headphone", TSP_NORMAL, gpio_headphone_monitor);
THREAD_DESCRIPTOR("Internal Microphone", TSP_NORMAL, gpio_microphone_monitor);

