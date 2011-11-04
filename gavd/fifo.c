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
#include "fifo.h"

LINKERSET_DECLARE(fifo_entry);

typedef struct fifo_node_t fifo_node_t;
struct fifo_node_t {
    fifo_node_t        *wfn_prev;
    fifo_node_t        *wfn_next;
    const fifo_entry_t *wfn_entry;
    void                   *wfn_data;
};

/* wf: work fifo
 *
 * inv: wf != NULL
 * inv: wfn->next == wf -> fifo empty
 * inv: fifo empty      -> wf->wfn_next == wf->wfn_prev
 */
static fifo_node_t *wf;
static pthread_mutex_t  wf_mutex;

static void fifo_lock(void)
{
    int result = pthread_mutex_lock(&wf_mutex);
    assert(result == 0);
}

static void fifo_unlock(void)
{
    int result = pthread_mutex_unlock(&wf_mutex);
    assert(result == 0);
}

static fifo_node_t *fifo_allocate_node(void)
{
    return calloc((size_t)1, sizeof(fifo_node_t));
}

static unsigned fifo_empty(void)
{
    unsigned empty = wf->wfn_next == wf;
    assert(!empty || wf->wfn_next == wf->wfn_prev);
    return empty;
}

static void fifo_delete_node(fifo_node_t *node)
{
    fifo_lock();
    node->wfn_prev->wfn_next = node->wfn_next;
    node->wfn_next->wfn_prev = node->wfn_prev;
    free(node);
    fifo_unlock();
}

static void fifo_append_node(fifo_node_t *node)
{
    node->wfn_next  = wf;
    node->wfn_prev  = wf->wfn_prev;

    fifo_lock();
    wf->wfn_prev->wfn_next = node;
    wf->wfn_prev           = node;
    fifo_unlock();
}

static void fifo_print_possible_events(void)
{
    /* Print all possible work fifo events that are included in the
     * executable.  This may not be the same for every build type due
     * to conditional compilation.
     */
    LINKERSET_ITERATE(fifo_entry, entry, {
            verbose_log(8, LOG_INFO, "%s: event: '%s'",
                        __FUNCTION__, entry->fe_name);
        });
}

static void fifo_create(void)
{
    /* Fifo is a doubly linked list, with a dummy head node. */
    wf = fifo_allocate_node();
    assert(wf != NULL);   /* Cannot create work fifo -> cannot run. */
    wf->wfn_next = wf;
    wf->wfn_prev = wf;
    pthread_mutex_init(&wf_mutex, NULL);
    fifo_print_possible_events();
}

static void fifo_destroy(void)
{
    while (!fifo_empty()) {
        fifo_delete_node(wf->wfn_next);
    }
    fifo_lock();
    free(wf);
    wf = NULL;
    fifo_unlock();
    pthread_mutex_destroy(&wf_mutex);
}

unsigned fifo_add_item(const fifo_entry_t *entry, void *data)
{
    fifo_node_t *node = fifo_allocate_node();
    if (node != NULL) {
        node->wfn_entry = entry;
        node->wfn_data  = data;
        fifo_append_node(node);
    }
    return node != NULL;
}

static void fifo_monitor_work(const char *thread_name)
{
    VERBOSE_FUNCTION_ENTER("%s", thread_name);

    while (!thread_management.tm_exit) {
        struct timeval timeout;

        if (!fifo_empty()) {
            fifo_node_t *node = wf->wfn_next;
            verbose_log(5, LOG_INFO, "%s: %s",
                        __FUNCTION__, node->wfn_entry->fe_name);
            node->wfn_entry->fe_handler(node->wfn_data);

            fifo_delete_node(node);
        }
        timeout.tv_sec  = 0;
        timeout.tv_usec = 250000;   /* 1/4 second. */
        select(0 + 1, NULL, NULL, NULL, &timeout);
    }

    VERBOSE_FUNCTION_EXIT("%s", thread_name);
}

static void *fifo_monitor(void *arg)
{
    thread_descriptor_t *desc = (thread_descriptor_t *)arg;

    /* Initialization Code. */
    pthread_barrier_wait(&thread_management.tm_create_barrier);

    /* Wait for all other threads to start. */
    pthread_barrier_wait(&thread_management.tm_start_barrier);
    fifo_monitor_work(desc->td_name);
    return NULL;
}

THREAD_DESCRIPTOR("Work FIFO", TSP_NORMAL, fifo_monitor);
INITIALIZER("Initialize Work FIFO", fifo_create, fifo_destroy);

