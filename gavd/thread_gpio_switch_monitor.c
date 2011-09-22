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

#include "board.h"
#include "verbose.h"
#include "sys_input.h"
#include "thread_management.h"
#include "gpio_switch_monitor.h"

#if defined(ADHD_GPIO_HEADPHONE)
static void *gpio_headphone_monitor(void *arg)
{
    thread_descriptor_t *desc = (thread_descriptor_t *)arg;
    gpio_switch_monitor(desc->name,
                        ADHD_INPUT_NAME_HEADPHONE_JACK,
                        SW_HEADPHONE_INSERT);
    return NULL;
}

THREAD_DESCRIPTOR("Internal Headphone", gpio_headphone_monitor);
#endif

#if defined(ADHD_GPIO_MICROPHONE)
static void *gpio_microphone_monitor(void *arg)
{
    thread_descriptor_t *desc = (thread_descriptor_t *)arg;
    gpio_switch_monitor(desc->name,
                        ADHD_INPUT_NAME_MICROPHONE_JACK,
                        SW_MICROPHONE_INSERT);
    return NULL;
}

THREAD_DESCRIPTOR("Internal Microphone", gpio_microphone_monitor);
#endif
