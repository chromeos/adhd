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

LINKERSET_DECLARE(workfifo_entry);

typedef struct workfifo_node_t workfifo_node_t;
struct workfifo_node_t {
    workfifo_node_t        *wfn_prev;
    workfifo_node_t        *wfn_next;
    const workfifo_entry_t *wfn_entry;
    void                   *wfn_data;
};

/* wf: work fifo
 *
 * inv: wf != NULL
 * inv: wfn->next == wf -> fifo empty
 * inv: fifo empty      -> wf->wfn_next == wf->wfn_prev
 */
static workfifo_node_t *wf;
static pthread_mutex_t  wf_mutex;

static void workfifo_lock(void)
{
    int result = pthread_mutex_lock(&wf_mutex);
    assert(result == 0);
}

static void workfifo_unlock(void)
{
    int result = pthread_mutex_unlock(&wf_mutex);
    assert(result == 0);
}

static workfifo_node_t *workfifo_allocate_node(void)
{
    return calloc((size_t)1, sizeof(workfifo_node_t));
}

static unsigned workfifo_empty(void)
{
    unsigned empty = wf->wfn_next == wf;
    assert(!empty || wf->wfn_next == wf->wfn_prev);
    return empty;
}

static void workfifo_delete_node(workfifo_node_t *node)
{
    workfifo_lock();
    node->wfn_prev->wfn_next = node->wfn_next;
    node->wfn_next->wfn_prev = node->wfn_prev;
    free(node);
    workfifo_unlock();
}

static void workfifo_append_node(workfifo_node_t *node)
{
    node->wfn_next  = wf;
    node->wfn_prev  = wf->wfn_prev;

    workfifo_lock();
    wf->wfn_prev->wfn_next = node;
    wf->wfn_prev           = node;
    workfifo_unlock();
}

static void workfifo_print_possible_events(void)
{
    /* Print all possible work fifo events that are included in the
     * executable.  This may not be the same for every build type due
     * to conditional compilation.
     */
    LINKERSET_ITERATE(workfifo_entry, entry, {
            verbose_log(8, LOG_INFO, "%s: event: '%s'",
                        __FUNCTION__, entry->wf_name);
        });
}

static void workfifo_create(void)
{
    /* Workfifo is a doubly linked list, with a dummy head node. */
    wf = workfifo_allocate_node();
    assert(wf != NULL);   /* Cannot create work fifo -> cannot run. */
    wf->wfn_next = wf;
    wf->wfn_prev = wf;
    pthread_mutex_init(&wf_mutex, NULL);
    workfifo_print_possible_events();
}

static void workfifo_destroy(void)
{
    while (!workfifo_empty()) {
        workfifo_delete_node(wf->wfn_next);
    }
    workfifo_lock();
    free(wf);
    wf = NULL;
    workfifo_unlock();
    pthread_mutex_destroy(&wf_mutex);
}

unsigned workfifo_add_item(const workfifo_entry_t *entry, void *data)
{
    workfifo_node_t *node = workfifo_allocate_node();
    if (node != NULL) {
        node->wfn_entry = entry;
        node->wfn_data  = data;
        workfifo_append_node(node);
    }
    return node != NULL;
}

static void workfifo_monitor_work(const char *thread_name)
{
    VERBOSE_FUNCTION_ENTER("%s", thread_name);

    while (!thread_management.tm_exit) {
        struct timeval timeout;

        if (!workfifo_empty()) {
            workfifo_node_t *node = wf->wfn_next;
            verbose_log(5, LOG_INFO, "%s: %s",
                        __FUNCTION__, node->wfn_entry->wf_name);
            node->wfn_entry->wf_handler(node->wfn_data);

            workfifo_delete_node(node);
        }
        timeout.tv_sec  = 0;
        timeout.tv_usec = 250000;   /* 1/4 second. */
        select(0 + 1, NULL, NULL, NULL, &timeout);
    }

    VERBOSE_FUNCTION_EXIT("%s", thread_name);
}

static void *workfifo_monitor(void *arg)
{
    thread_descriptor_t *desc = (thread_descriptor_t *)arg;

    /* Initialization Code. */
    pthread_barrier_wait(&thread_management.tm_create_barrier);

    /* Wait for all other threads to start. */
    pthread_barrier_wait(&thread_management.tm_start_barrier);
    workfifo_monitor_work(desc->td_name);
    return NULL;
}

THREAD_DESCRIPTOR("Work FIFO", TSP_NORMAL, workfifo_monitor);
INITIALIZER("Initialize Work FIFO", workfifo_create, workfifo_destroy);

