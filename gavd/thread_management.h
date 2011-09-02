/* Copyright (c) 2011 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
#if !defined(_THREAD_MANAGEMENT_H_)
#define _THREAD_MANAGEMENT_H_
#include <pthread.h>

typedef struct thread_descriptor_t {
    void       *(*start_routine)(void*); /* pthread start routine */
    const char *name;

    pthread_t   thread;
    void       *data;
} thread_descriptor_t;

#define THREAD_DESCRIPTOR(_name, _start_routine)                        \
    static thread_descriptor_t __thread_descriptor_##_start_routine = { \
        .name          = _name,                                         \
        .start_routine = _start_routine,                                \
    };                                                                  \
    __asm__(".global __start_thread_descriptor");                       \
    __asm__(".global __stop_thread_descriptor");                        \
    static void const * const __thread_descriptor_ptr_##_start_routine  \
    __attribute__((section("thread_descriptor"),used)) =                \
         &__thread_descriptor_##_start_routine

typedef struct thread_management_t {
    unsigned      quit;         /* quit == 0 => Daemon continues to run.
                                 * quit      != 1 => Daemon exits.
                                 */
    unsigned      exit;         /* exit == 0 => Continue running.
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
#endif
