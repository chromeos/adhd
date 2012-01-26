
/* Copyright (c) 2011 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
#include <assert.h>
#include <pthread.h>
#include <limits.h>
#include <sys/time.h>

#include "board.h"
#include "set_factory_default.h"
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
    const unsigned n_threads = LINKERSET_SIZE(thread_descriptor, unsigned);
    struct timeval beg;
    struct timeval end;

    thread_management.tm_exit = 0;
    thread_management.tm_quit = 0;

    assert((ptrdiff_t)n_threads == /* Truncation? */
           LINKERSET_SIZE_PTRDIFF(thread_descriptor));

    /* The thread descriptors are sorted in order of priorty.  There
     * is no ordering in a priority level, but lower priorities can
     * rely on the fact that high priorities have already started.
     */

    initialization_initialize();

    /* tm_hardware: Lock only when modifying Alsa hardware state. */
    pthread_mutex_init(&thread_management.tm_hardware, NULL);

    /* To ensure that each thread gets to start up in priority order,
     * and with no race conditions for initialiation, two barriers are
     * used.
     *
     *  o creation barrier
     *
     *    Each thread will execute its startup code and reach a
     *    barrier shared only by the loop iterating over the set of
     *    threads.
     *
     *    This is most notably used by the thread which resets the
     *    internal sound card to the 'factory default' values.
     *
     *  o startup barrier
     *
     *    After the thread has completed its initialiation and passed
     *    the 'creation' barrier, it will wait for all threads to be
     *    started at the 'startup' barrier.  The startup barrier is
     *    shared between all threads and this code.
     *
     * After both barriers are passed, all threads will begin running
     * normally.
     */
    pthread_barrier_init(&thread_management.tm_start_barrier, NULL,
                         n_threads /* Declared threads. */ +
                         1         /* This function.    */);

    gettimeofday(&beg, NULL);
    LINKERSET_ITERATE(thread_descriptor, desc, {
            verbose_log(1, LOG_INFO, "%s: '%s'",
                        __FUNCTION__, desc->td_name);
            pthread_barrier_init(&thread_management.tm_create_barrier, NULL,
                                 1 /* Created thread. */ +
                                 1 /* This function.  */);
            pthread_create(&desc->td_thread, NULL,
                           desc->td_entry, desc);
            pthread_barrier_wait(&thread_management.tm_create_barrier);
            pthread_barrier_destroy(&thread_management.tm_create_barrier);
        });
    gettimeofday(&end, NULL);
    verbose_log(0, LOG_INFO, "%s: time to start %u threads: %u.%u seconds",
                __FUNCTION__, n_threads,
                (unsigned)(end.tv_sec - beg.tv_sec),
                (unsigned)(end.tv_usec - beg.tv_usec));

    /* All threads are waiting at the 'tm_start_barrier' barrier.
     *
     * Before reaching the barrier here, we can dope data structures
     * which will be used by the threads to affect actions which must
     * be done on startup.
     *
     * For example, adding an element to a worklist to reset the
     * internal hardware to the 'factory default' values.
     */
    factory_default_add_event();

    pthread_barrier_wait(&thread_management.tm_start_barrier);
    verbose_log(5, LOG_INFO, "%s: start barrier passed.\n", __FUNCTION__);
    pthread_barrier_destroy(&thread_management.tm_start_barrier);
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
    pthread_mutex_destroy(&thread_management.tm_hardware);
    initialization_finalize();
    thread_management.tm_exit = 0;
}

unsigned threads_quit_daemon(void)
{
    return thread_management.tm_quit;
}

void threads_lock_hardware(void)
{
    VERBOSE_FUNCTION_ENTER("%s", "void");
    pthread_mutex_lock(&thread_management.tm_hardware);
    VERBOSE_FUNCTION_EXIT("%s", "void");
}

void threads_unlock_hardware(void)
{
    VERBOSE_FUNCTION_ENTER("%s", "void");
    pthread_mutex_unlock(&thread_management.tm_hardware);
    VERBOSE_FUNCTION_EXIT("%s", "void");
}
