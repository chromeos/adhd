
/* Copyright (c) 2011 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
#include <assert.h>
#include <signal.h>
#include <stdlib.h>

#include "verbose.h"
#include "thread_management.h"
#include "signal_handling.h"

static void signal_handle_sighup(int        signum,
                                 siginfo_t *siginfo,
                                 void      *ucontext)
{
    verbose_log(8, LOG_INFO, "%s", __FUNCTION__);
    signum   = signum;          /* Silence compiler warning. */
    siginfo  = siginfo;         /* Silence compiler warning. */
    ucontext = ucontext;        /* Silence compiler warning. */
    threads_kill_all();
    threads_start();
}

static void signal_start_sighup(void)
{
    struct sigaction action;
    sigset_t         sa_mask;

    sigemptyset(&sa_mask);
    action.sa_mask      = sa_mask;
    action.sa_flags     = SA_SIGINFO; /* Use sa.sigaction, not sa_handler. */
    action.sa_sigaction = signal_handle_sighup;
    sigaction(SIGHUP, &action, NULL);
}

static void signal_handle_sigterm(int        signum,
                                  siginfo_t *siginfo,
                                  void      *ucontext)
{
    verbose_log(8, LOG_INFO, "%s", __FUNCTION__);
    signum   = signum;          /* Silence compiler warning. */
    siginfo  = siginfo;         /* Silence compiler warning. */
    ucontext = ucontext;        /* Silence compiler warning. */
    threads_kill_all();
    thread_management.tm_quit = 1;
}

static void signal_start_sigterm(void)
{
    struct sigaction action;
    sigset_t         sa_mask;

    sigemptyset(&sa_mask);
    action.sa_mask      = sa_mask;
    action.sa_flags     = SA_SIGINFO; /* Use sa.sigaction, not sa_handler. */
    action.sa_sigaction = signal_handle_sigterm;
    sigaction(SIGTERM, &action, NULL);
}

void signal_start(void)
{
    verbose_log(5, LOG_INFO, "%s", __FUNCTION__);
    signal_start_sighup();
    signal_start_sigterm();
}
