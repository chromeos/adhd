/* Copyright (c) 2011 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
#include <stdarg.h>
#include <stdio.h>
#include <syslog.h>
#include "verbose.h"

static unsigned  verbose_level = 0;
static const int log_options   = LOG_NDELAY | LOG_PID;
static const int log_facility  = LOG_DAEMON; /* /var/log/daemon.log */
static char      log_buffer[1024];


void verbose_set(unsigned verbosity)
{
    verbose_level = verbosity;
}

void verbose_initialize(const char *program_name)
{
    openlog(program_name, log_options, log_facility);
}

void verbose_finalize(void)
{
    closelog();
}


/*
 *  log_level values:
*/
void verbose_log(unsigned    verbosity_level,
                 int         log_level,
                 const char *format,
                 ...)
{
    if (verbosity_level <= verbose_level) {
        const size_t log_buffer_len = (sizeof(log_buffer) /
                                       sizeof(log_buffer[0]));
        va_list ap;

        va_start(ap, format);
        vsnprintf(log_buffer, log_buffer_len, format, ap);
        log_buffer[log_buffer_len - 1] = '\0';
        va_end(ap);
        syslog(log_facility | log_level, "%s", log_buffer);
    }
}
