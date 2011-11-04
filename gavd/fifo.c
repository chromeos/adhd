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

static void fifo_lock(fifo_t *fifo)
{
    int result = pthread_mutex_lock(&fifo->fifo_mutex);
    assert(result == 0);
}

static void fifo_unlock(fifo_t *fifo)
{
    int result = pthread_mutex_unlock(&fifo->fifo_mutex);
    assert(result == 0);
}

static fifo_node_t *fifo_allocate_node(void)
{
    return calloc((size_t)1, sizeof(fifo_node_t));
}

static fifo_t *fifo_allocate(void)
{
    return calloc((size_t)1, sizeof(fifo_t));
}

static unsigned fifo_empty(fifo_t *fifo)
{
    unsigned empty = fifo->fifo_head->fn_next == fifo->fifo_head;
    assert(!empty || fifo->fifo_head->fn_next == fifo->fifo_head->fn_prev);
    return empty;
}

static void fifo_delete_node(fifo_t *fifo, fifo_node_t *node)
{
    fifo_lock(fifo);
    node->fn_prev->fn_next = node->fn_next;
    node->fn_next->fn_prev = node->fn_prev;
    free(node);
    fifo_unlock(fifo);
}

static void fifo_append_node(fifo_t      *fifo,
                             fifo_node_t *node)
{
    fifo_lock(fifo);
    node->fn_next = fifo->fifo_head;
    node->fn_prev = fifo->fifo_head->fn_prev;

    fifo->fifo_head->fn_prev->fn_next = node;
    fifo->fifo_head->fn_prev          = node;
    fifo_unlock(fifo);
}

fifo_t *fifo_create(void)
{
    /* A fifo is a doubly linked list, with a dummy head node. */
    fifo_t      *fifo = fifo_allocate();
    fifo_node_t *head = fifo_allocate_node();
    assert(fifo != NULL);      /* Cannot create fifo -> cannot run. */
    assert(head != NULL);      /* Cannot create fifo -> cannot run. */

    head->fn_next = head;
    head->fn_prev = head;
    fifo->fifo_head = head;
    pthread_mutex_init(&fifo->fifo_mutex, NULL);
    return fifo;
}

void fifo_destroy(fifo_t *fifo)
{
    while (!fifo_empty(fifo)) {
        fifo_delete_node(fifo, fifo->fifo_head->fn_next);
    }
    fifo_delete_node(fifo, fifo->fifo_head);
    fifo_lock(fifo);
    fifo->fifo_head = NULL;
    fifo_unlock(fifo);
    pthread_mutex_destroy(&fifo->fifo_mutex);
    free(fifo);
}

unsigned __fifo_add_item(fifo_t             *fifo,
                         const fifo_entry_t *entry,
                         void               *data)
{
    fifo_node_t *node = fifo_allocate_node();
    if (node != NULL) {
        node->fn_entry = entry;
        node->fn_data  = data;
        fifo_append_node(fifo, node);
    }
    return node != NULL;
}

void fifo_monitor_work(const char *thread_name,
                       fifo_t     *fifo,
                       useconds_t  sleep_usec)
{
    VERBOSE_FUNCTION_ENTER("%s", thread_name);

    while (!thread_management.tm_exit) {
        if (!fifo_empty(fifo)) {
            fifo_node_t *node = fifo->fifo_head->fn_next;
            verbose_log(5, LOG_INFO, "%s: %s",
                        __FUNCTION__, node->fn_entry->fe_name);
            node->fn_entry->fe_handler(node->fn_data);

            fifo_delete_node(fifo, node);
        }
        usleep(sleep_usec);
    }

    VERBOSE_FUNCTION_EXIT("%s", thread_name);
}
