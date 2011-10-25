
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

/* [__start_thread_descriptor, __stop_thread_descriptor) is an array
 * of pointers which reference the actual thread descriptors.
 *
 * If no threads are defined, both symbols will be NULL; be sure to
 * take this detail into consideration when traversing the set of
 * threads.
 */
LINKERSET_DECLARE(thread_descriptor);

thread_management_t thread_management;

void threads_start(void)
{
    thread_management.tm_exit = 0;
    thread_management.tm_quit = 0;
    initialization_initialize();
    LINKERSET_ITERATE(thread_descriptor, desc,
                      {
                          verbose_log(1, LOG_INFO, "%s: '%s'",
                                      __FUNCTION__, desc->td_name);
                          pthread_create(&desc->td_thread, NULL,
                                         desc->td_start_routine, desc);
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
