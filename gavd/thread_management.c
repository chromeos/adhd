
/* Copyright (c) 2011 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
#include <assert.h>
#include <pthread.h>
#include <limits.h>

#include "board.h"
#include "verbose.h"
#include "thread_management.h"

#define WEAK __attribute__((weak))

/* [__start_thread_descriptor, __stop_thread_descriptor) is an array
 * of pointers which reference the actual thread descriptors.
 *
 * If no threads are defined, both symbols will be NULL; be sure to
 * take this detail into consideration when traversing the set of
 * threads.
 */
extern thread_descriptor_t WEAK *__start_thread_descriptor;
extern thread_descriptor_t WEAK *__stop_thread_descriptor;

thread_management_t thread_management;

static thread_descriptor_t **threads_descriptor_start(void)
{
    return &__start_thread_descriptor;
}

static thread_descriptor_t **threads_descriptor_stop(void)
{
    return &__stop_thread_descriptor;
}

#define FOREACH_THREAD(_body)                                   \
    {                                                           \
    thread_descriptor_t **_beg = threads_descriptor_start();    \
    thread_descriptor_t **_end = threads_descriptor_stop();     \
    while (_beg < _end) {                                       \
        /* 'desc' variable is available for use in '_body' */   \
        thread_descriptor_t *desc = *_beg;                      \
        verbose_log(1, LOG_INFO, "%s: '%s'",                    \
                    __FUNCTION__, desc->name);                  \
        _body;                                                  \
        ++_beg;                                                 \
    }                                                           \
}

void threads_start(void)
{
    thread_management.exit = 0;
    thread_management.quit = 0;
    FOREACH_THREAD({
        pthread_create(&desc->thread, NULL, desc->start_routine, desc);
        });
}

void threads_kill_all(void)
{
    thread_management.exit = 1;
    FOREACH_THREAD({
        pthread_join(desc->thread, NULL);
        });
    thread_management.exit = 0;
}

unsigned threads_quit_daemon(void)
{
    return thread_management.quit;
}
