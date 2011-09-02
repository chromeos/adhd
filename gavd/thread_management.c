
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

extern unsigned char __thread_descriptor_beg;
extern unsigned char __thread_descriptor_end;

thread_management_t thread_management;

static unsigned threads_number_of_descriptors(void)
{
    unsigned long length = ((unsigned long)(&__thread_descriptor_end -
                                            &__thread_descriptor_beg) /
                            sizeof(thread_descriptor_t));

    assert(length <= UINT_MAX);
    return (unsigned)length;
}

static thread_descriptor_t *threads_first_descriptor(void)
{
    return (thread_descriptor_t *)&__thread_descriptor_beg;
}

static thread_descriptor_t *threads_last_descriptor(void)
{
    unsigned             n    = threads_number_of_descriptors();
    thread_descriptor_t *desc = threads_first_descriptor() + (n - 1);
    return desc;
}

void threads_start(void)
{
    thread_descriptor_t *desc = threads_first_descriptor();
    unsigned             n    = threads_number_of_descriptors();

    thread_management.exit = 0;
    thread_management.quit = 0;
    while (n-- > 0) {
        verbose_log(1, LOG_INFO, "%s: '%s'", __FUNCTION__, desc->name);
        pthread_create(&desc->thread, NULL, desc->start_routine, desc);
        ++desc;
    }
}

void threads_kill_all(void)
{
    /* Threads are killed in the reverse order of creation. */
    unsigned             n    = threads_number_of_descriptors();
    thread_descriptor_t *desc = threads_last_descriptor();

    thread_management.exit = 1;
    while (n-- > 0) {
        verbose_log(5, LOG_INFO, "%s: '%s'", __FUNCTION__, desc->name);
        pthread_join(desc->thread, NULL);
        --desc;
    }
    thread_management.exit = 0;
}

unsigned threads_quit_daemon(void)
{
    return thread_management.quit;
}
