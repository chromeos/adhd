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
#include "dbus_to_chrome_fifo.h"

typedef struct state_t {
    unsigned state;             /* 0     -> Speaker enabled, headphone disabled.
                                 * 1     -> Speaker disabled, headphone enabled.
                                 * other -> llegal.
                                 */
} state_t;

FIFO_DEFINE(dbus_to_chrome_fifo)

static void *dbus_to_chrome_fifo_monitor(void *arg)
{
    thread_descriptor_t *desc = (thread_descriptor_t *)arg;

    /* Initialization Code. */
    pthread_barrier_wait(&thread_management.tm_create_barrier);

    /* Wait for all other threads to start. */
    pthread_barrier_wait(&thread_management.tm_start_barrier);
    fifo_monitor_work(desc->td_name, dbus_to_chrome_fifo, 250000);
    return NULL;
}

static void dbus_to_chrome_fifo_create(void)
{
    dbus_to_chrome_fifo = fifo_create();
    FIFO_ELEMENT_ITERATE(dbus_to_chrome_fifo, entry, {
            verbose_log(8, LOG_INFO, "%s: event: '%s'",
                        __FUNCTION__, entry->entry.fe_name);
        });
}

static void dbus_to_chrome_fifo_destroy(void)
{
    fifo_destroy(dbus_to_chrome_fifo);
}

FIFO_ENTRY("Internal Speaker/Headphone State",
           dbus_to_chrome_fifo,
           internal_speaker_headphone_state,
{
    const char *speaker;
    const char *headphone;
    state_t    *state = (state_t *)data;

    switch (state->state) {
    case 0:
        speaker   = "on";
        headphone = "off";
        break;
    case 1:
        speaker   = "off";
        headphone = "on";
        break;

    default:
        speaker   = "invalid";
        headphone = "invalid";
        break;
    }

    verbose_log(0, LOG_INFO, "%s: speaker: %s.  headphone: %s", __FUNCTION__,
                speaker, headphone);
    free(data);
});

void dbus_to_chrome_fifo_internal_speaker_headphone_state(unsigned state)
{
    state_t *data = calloc((size_t)1, sizeof(state_t));

    assert(state == 0 || state == 1);
    if (data != NULL) {
        data->state = state;
        if (!FIFO_ADD_ITEM(dbus_to_chrome_fifo,
                           internal_speaker_headphone_state, data)) {
            free(data);
        }
    }
}

THREAD_DESCRIPTOR("DBus-to-Chrome FIFO", TSP_NORMAL,
                  dbus_to_chrome_fifo_monitor);
INITIALIZER("DBus-to-Chrome-FIFO", dbus_to_chrome_fifo_create,
            dbus_to_chrome_fifo_destroy);

