/* Copyright (c) 2011 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
#if !defined(_THREAD_MANAGEMENT_H_)
#define _THREAD_MANAGEMENT_H_
#include <pthread.h>
#include "linkerset.h"

#define THREAD_STARTUP_PRIORITIES                       \
    TSP(INITIALIZE)                                     \
    TSP(NORMAL)

typedef enum thread_startup_priority_t {
#define TSP(_tp) TSP_##_tp,
    THREAD_STARTUP_PRIORITIES
#undef TSP
    N_THREAD_PRIORITIES
} thread_startup_priority_t;

typedef struct thread_descriptor_t {
    void                      *(*td_entry)(void*); /* pthread entry point */
    const char                *td_name;
    thread_startup_priority_t  td_priority;

    pthread_t                  td_thread;
    void                      *td_data;
} thread_descriptor_t;

/*  _name : String literal name for the thread.
 *  _pri  : thread_startup_priority_t for the thread.
 *  _entry: Function used to start the pthread.
 */
#define THREAD_DESCRIPTOR(_name, _pri, _entry)                  \
    static thread_descriptor_t  /* Cannot be 'const'. */        \
    __thread_descriptor_##_entry = {                            \
        .td_name     = _name,                                   \
        .td_entry    = _entry,                                  \
        .td_priority = _pri,                                    \
    };                                                          \
    LINKERSET_ADD_ITEM(thread_descriptor,                       \
                       __thread_descriptor_##_entry)

typedef struct thread_management_t {
    volatile unsigned tm_quit;  /* quit == 0 => Daemon continues to run.
                                 * quit      != 1 => Daemon exits.
                                 */
    volatile unsigned tm_exit;  /* exit == 0 => Continue running.
                                 * exit      != 0 => Thread should exit.
                                 *
                                 * There is no mutex controlling this
                                 * data because it is written by one
                                 * function.  All readers will exit
                                 * when a non-zero value is present.
                                 */
} thread_management_t;

extern thread_management_t thread_management;

void threads_start(void);
void threads_kill_all(void);
unsigned threads_quit_daemon(void);
void threads_sort_descriptors(void);
#endif
