
/* Copyright (c) 2011 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
#include <assert.h>
#include <pthread.h>
#include <limits.h>

#include "board.h"
#include "initialization.h"
#include "verbose.h"
#include "linkerset.h"
#include "thread_management.h"

LINKERSET_DECLARE(thread_descriptor);
thread_management_t thread_management;

static int thread_descriptor_priority_compare(const void *larg,
                                              const void *rarg)
{
    const thread_descriptor_t * const *lp = larg;
    const thread_descriptor_t * const *rp = rarg;
    const thread_descriptor_t         *l  = *lp;
    const thread_descriptor_t         *r  = *rp;
    return (int)l->td_priority - (int)r->td_priority;
}

void threads_sort_descriptors(void)
{
    /* Sort thread descriptors only once during initialization. */
    LINKERSET_SORT(thread_descriptor,
                   thread_descriptor_priority_compare);
    verbose_log(5, LOG_INFO, "%s: thread descriptors sorted.\n",
                __FUNCTION__);
}

void threads_start(void)
{
    thread_management.tm_exit = 0;
    thread_management.tm_quit = 0;

    /* The thread descriptors are sorted in order of priorty.  There
     * is no ordering in a priority level, but lower priorities can
     * rely on the fact that high priorities have already started.
     */

    initialization_initialize();

    LINKERSET_ITERATE(thread_descriptor, desc, {
            verbose_log(1, LOG_INFO, "%s: '%s'",
                        __FUNCTION__, desc->td_name);
            pthread_create(&desc->td_thread, NULL,
                           desc->td_entry, desc);
        });
}

void threads_kill_all(void)
{
    thread_management.tm_exit = 1;
    LINKERSET_ITERATE(thread_descriptor, desc,
                      {
                          verbose_log(1, LOG_INFO, "%s: '%s'",
                                      __FUNCTION__, desc->td_name);
                          pthread_join(desc->td_thread, NULL);
                      });
    initialization_finalize();
    thread_management.tm_exit = 0;
}

unsigned threads_quit_daemon(void)
{
    return thread_management.tm_quit;
}
