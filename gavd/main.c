/* Copyright (c) 2011 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <errno.h>
#include <getopt.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <unistd.h>
#include "verbose.h"
#include "signal_handling.h"
#include "thread_management.h"
#include "board.h"

static const char * const program_name = "gavd";

static void help(void)
{
    /* TODO(thutt): Add help */
}

static void process_arguments(int argc, char **argv)
{
    static struct option options[] = {
        {
            .name    = "help",
            .has_arg = no_argument,
            .flag    = NULL,
            .val     = 256
        },
        {
            .name    = "verbose",
            .has_arg = optional_argument,
            .flag    = NULL,
            .val     = 257
        },
    };

    while (1) {
        int option_index = 0;
        const int choice = getopt_long(argc, argv, "", options, &option_index);

        if (choice == -1) {
            break;
        }

        switch (choice) {
        case 256:
            help();
            break;

        case 257:
            /* Goobuntu has a defect with 'optarg'.  If '--verbose 1'
             * is used, 'optarg' will be NULL.  Use '--verbose=1'.
             */
            verbose_set((unsigned)atoi(optarg));
            break;

        default:
            printf("?? getopt returned character code 0%o ??\n", choice);
        }
    }
}

static void daemonize(void)
{
    pid_t child_pid;

    child_pid = fork();
    if (child_pid != 0) {
        verbose_log(0, LOG_INFO, "Child process: '%u'.", child_pid);
        exit(0);
    }

    /* Now running as daemon.
     *
     * TODO(thutt): Detach from console.
     * TODO(thutt): close stdin/stdout/stderr
     */
    verbose_log(0, LOG_INFO, "%s: started", __FUNCTION__);
    if (chdir("/") != 0) {
        verbose_log(0, LOG_ERR, "Failed to chdir('/')");
        exit(-errno);
    }        

    signal_start();
    threads_start();

    while(!threads_quit_daemon()) {
        sleep(3);
    }
    
    verbose_log(0, LOG_INFO, "%s: stopped", program_name);
}

int main(int argc, char **argv)
{
    process_arguments(argc, argv);
    verbose_initialize(program_name);

    verbose_log(0, LOG_INFO, "compiled for target machine: '%s'",
                ADHD_TARGET_MACHINE);

    daemonize();

    verbose_finalize();
    return 0;
}
