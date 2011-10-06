/* Copyright (c) 2011 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <errno.h>
#include <fcntl.h>
#include <getopt.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/resource.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/types.h>
#include <unistd.h>

#include "board.h"
#include "adhd_alsa.h"
#include "signal_handling.h"
#include "thread_management.h"
#include "verbose.h"

static const char * const program_name = "gavd";

/*
 * arg_release_mode == 0 -> {stdin, stdout, stderr} <=> original descriptors
 * arg_release_mode == 1 -> {stdin, stdout, stderr} <=> /dev/null
 */
static unsigned arg_release_mode = 1;

static void help(void)
{
    static char const * const msg[] = {
        "gavd [options]...",
        "",
        "Google A/V Daemon",
        "",
        "  options := --help              |",
        "             --developer         |",
        "             --verbose=<integer>",
        "",
        "  --help     : Produces this help message.",
        "  --developer: Runs the daemon in developer mode.",
        "  --verbose  : Set the verbosity level to <integer>.",
        "               0 is the default, and provides minimal",
        "               logging.  Greater numbers provide greater",
        "               verbosity."
        "",
        "All messages produced by this daemon are output using the",
        "syslog service.",
        "",
        "",
    };
    unsigned i;

    for (i = 0; i < sizeof(msg) / sizeof(msg[0]); ++i) {
        fprintf(stderr, "%s\n", msg[i]);
    }
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
        {
            .name    = "developer",
            .has_arg = no_argument,
            .flag    = NULL,
            .val     = 258
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

        case 258:
            arg_release_mode = 0;
            break;

        default:
            printf("?? getopt returned character code 0%o ??\n", choice);
        }
    }
}

static void setup_release_environment(void)
{
    /*
     * TODO(thutt): Detach from console.
     */
    if (chdir("/") != 0) {
        verbose_log(0, LOG_ERR, "Failed to chdir('/')");
        exit(-errno);
    }
}

static void daemonize(void)
{
    pid_t child_pid;
    const char *operational_mode;

    VERBOSE_FUNCTION_ENTER("%s", "void")
    child_pid = fork();
    if (child_pid != 0) {
        verbose_log(0, LOG_INFO, "Child process: '%u'.", child_pid);
        exit(0);
    }

    /* Now running as daemon. */

    assert(arg_release_mode == 0 || arg_release_mode == 1);
    if (arg_release_mode) {
        operational_mode = "release";
        setup_release_environment();
    } else {
        operational_mode = "developer";
    }
    verbose_log(3, LOG_INFO, "%s: %s mode.", __FUNCTION__, operational_mode);

    signal_start();
    threads_start();

    while(!threads_quit_daemon()) {
        sleep(3);
    }

    VERBOSE_FUNCTION_EXIT("%s", "void")
}

int main(int argc, char **argv)
{
    process_arguments(argc, argv);
    verbose_initialize(program_name);

    verbose_log(0, LOG_INFO, "compiled for target machine: '%s'",
                ADHD_TARGET_MACHINE);

    daemonize();

    verbose_finalize();
    verbose_log(0, LOG_INFO, "deamon exited");
    return 0;
}
