/* Copyright (c) 2011 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * This module implements a FIFO that is used as a worklist.  All work
 * performed by gavd will flow through this worklist, and be processed
 * in order of arrival.
 *
 * The FIFO is implemented, for simplicity, as a doubly-linked list
 * with a dummy head.  The dummy head removes any special cases
 * associated with an empty FIFO.
 */
#include <assert.h>
#include <stdlib.h>

#include "initialization.h"
#include "thread_management.h"
#include "verbose.h"
#include "workfifo.h"

FIFO_DEFINE(workfifo)

static void *workfifo_monitor(void *arg)
{
    thread_descriptor_t *desc = (thread_descriptor_t *)arg;

    /* Initialization Code. */
    pthread_barrier_wait(&thread_management.tm_create_barrier);

    /* Wait for all other threads to start. */
    pthread_barrier_wait(&thread_management.tm_start_barrier);
    fifo_monitor_work(desc->td_name, workfifo, 250000);
    return NULL;
}

static void workfifo_create(void)
{
    workfifo = fifo_create();
    FIFO_ELEMENT_ITERATE(workfifo, entry, {
            verbose_log(8, LOG_INFO, "%s: event: '%s'",
                        __FUNCTION__, entry->entry.fe_name);
        });
}

static void workfifo_destroy(void)
{
    fifo_destroy(workfifo);
}

THREAD_DESCRIPTOR("Work FIFO", TSP_NORMAL, workfifo_monitor);
INITIALIZER("Work FIFO", workfifo_create, workfifo_destroy);

