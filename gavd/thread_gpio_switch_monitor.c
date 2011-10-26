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
#include "utils.h"
#include "gpio_switch_monitor.h"

#if defined(ADHD_GPIO_HEADPHONE)
static void *gpio_headphone_monitor(void *arg)
{
    thread_descriptor_t *desc = (thread_descriptor_t *)arg;

    /* Initialization Code. */
    pthread_barrier_wait(&thread_management.tm_create_barrier);

    /* Wait for all other threads to start. */
    pthread_barrier_wait(&thread_management.tm_start_barrier);
    gpio_switch_monitor(desc->td_name,
                        ADHD_INPUT_NAME_HEADPHONE_JACK,
                        SW_HEADPHONE_INSERT,
                        ADHD_GPIO_HEADPHONE_INSERT_COMMAND,
                        ADHD_GPIO_HEADPHONE_REMOVE_COMMAND);
    return NULL;
}

THREAD_DESCRIPTOR("Internal Headphone", TSP_NORMAL, gpio_headphone_monitor);
#endif

#if defined(ADHD_GPIO_MICROPHONE)
static void *gpio_microphone_monitor(void *arg)
{
    thread_descriptor_t *desc = (thread_descriptor_t *)arg;

    /* Initialization Code. */
    pthread_barrier_wait(&thread_management.tm_create_barrier);

    /* Wait for all other threads to start. */
    pthread_barrier_wait(&thread_management.tm_start_barrier);
    gpio_switch_monitor(desc->td_name,
                        ADHD_INPUT_NAME_MICROPHONE_JACK,
                        SW_MICROPHONE_INSERT,
                        NULL,
                        NULL);
    return NULL;
}

THREAD_DESCRIPTOR("Internal Microphone", TSP_NORMAL, gpio_microphone_monitor);
#endif
