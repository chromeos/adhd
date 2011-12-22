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
#include <string.h>

#include "initialization.h"
#include "thread_management.h"
#include "verbose.h"
#include "dbus_connection.h"
#include "chrome_card_info_fifo.h"

typedef struct info_t {
    const char *udev_sysname;   /* non-NULL card name */
    unsigned    action;         /* 0      => add
                                 * 1      => remove
                                 * 2      => change
                                 * others => invalid
                                 */
    unsigned    num;            /* card number        */
    unsigned    internal;       /* internal device    */
    unsigned    active;         /* current I/O device */
    unsigned    primary;        /* 'default' device   */
} info_t;

FIFO_DEFINE(chrome_card_info_fifo)

static void *csci_monitor(void *arg)
{
    thread_descriptor_t *desc = (thread_descriptor_t *)arg;

    /* Initialization Code. */
    pthread_barrier_wait(&thread_management.tm_create_barrier);

    /* Wait for all other threads to start. */
    pthread_barrier_wait(&thread_management.tm_start_barrier);
    fifo_monitor_work(desc->td_name, chrome_card_info_fifo, 250000);
    return NULL;
}

static void csci_create(void)
{
    chrome_card_info_fifo = fifo_create();
    FIFO_ELEMENT_ITERATE(chrome_card_info_fifo, entry, {
            verbose_log(8, LOG_INFO, "%s: event: '%s'",
                        __FUNCTION__, entry->entry.fe_name);
        });
}

static void csci_destroy(void)
{
    fifo_destroy(chrome_card_info_fifo);
}

static void free_info(info_t *p)
{
    char *s = (char *)p->udev_sysname;

    p->udev_sysname = NULL;
    free(s);
    free(p);
}

FIFO_ENTRY("Chrome: Send Card Add / Remove",
           chrome_card_info_fifo, card_status,
{
    info_t     *p = (info_t *)data;
    const char *action;

    assert(p != NULL);

    switch (p->action) {
    case 0:  action = "add";       break;
    case 1:  action = "remove";    break;
    case 2:  action = "change";    break;
    default: action = "<invalid>"; break;
    }
    verbose_log(5, LOG_INFO, "%s: action: %s  card: %s  num: %u  "
                "active: %u  internal:  %u  primary: %u",
                __FUNCTION__, action,
                p->udev_sysname, p->num, p->active, p->internal, p->primary);

    dbus_connection_card_state(p->action, p->udev_sysname,
                               p->num, p->active, p->internal, p->primary);
    free_info(p);
});

static void chrome_card_status(unsigned    action,
                               const char *udev_sysname,
                               unsigned    num,
                               unsigned    active,
                               unsigned    internal,
                               unsigned    primary)
{
    info_t *data = calloc((size_t)1, sizeof(info_t));

    assert(action < 3);         /* 0 -> add
                                 * 1 -> remove
                                 * 2 -> changed
                                 */
    if (data != NULL) {
        data->action       = action;
        data->udev_sysname = strdup(udev_sysname);
        data->num          = num;
        data->active       = active;
        data->internal     = internal;
        data->primary      = primary;

        if (!FIFO_ADD_ITEM(chrome_card_info_fifo, card_status, data)) {
            free_info(data);
        }
    }
}

void chrome_card_added(const char *udev_sysname, unsigned num)
{
    chrome_card_status(0, udev_sysname, num, 0, 0, 0);
}

void chrome_card_removed(const char *udev_sysname, unsigned num)
{
    chrome_card_status(1, udev_sysname, num, 0, 0, 0);
}

void chrome_card_changed(const char *udev_sysname,
                         unsigned    num,
                         unsigned    active,
                         unsigned    internal,
                         unsigned    primary)
{
    /* Some attribute has been changed.  It is the responsibility of
     * the listener to determine which attribute chagned.
     */
    chrome_card_status(2, udev_sysname, num, active, internal, primary);
}

THREAD_DESCRIPTOR("Chrome: Send Card Info FIFO", TSP_NORMAL,
                  csci_monitor);
INITIALIZER("Chrome: Send Card Info FIFO", csci_create,
            csci_destroy);
