/* Copyright (c) 2011 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#if !defined(_VERBOSE_H_)
#define _VERBOSE_H_
#include <syslog.h>

/* 'format' must be a string literal, because this macro relies on
 * string conactenation.
 */
#define VERBOSE_FUNCTION_ENTER(format, ...)             \
    verbose_log(5, LOG_INFO, "beg %s(" format ")",      \
                __FUNCTION__, __VA_ARGS__);

/* 'format' must be a string literal, because this macro relies on
 * string conactenation.
 */
#define VERBOSE_FUNCTION_EXIT(format, ...)              \
    verbose_log(5, LOG_INFO, "end %s(" format ")",      \
                __FUNCTION__, __VA_ARGS__);


/* verbose_set: Set verbosity of the daemon.
 *
 *  verbosity: The new level of verbosity.
 *             Higher levels mean more diagnostic output will be produced.
 *             If not called, the verbosity level is 0.
 *
 * A verbosity level of 0 means minimal output.
 */
void verbose_set(unsigned verbosity);

/* verbose_log: Log information using syslog
 *
 *   verbosity_level : Message is logged only if 'level <= current
 *                     verbosity level'.
 *   log_level       : LOG_EMERG      system is unusable
 *                     LOG_ALERT      action must be taken immediately
 *                     LOG_CRIT       critical conditions
 *                     LOG_ERR        error conditions
 *                     LOG_WARNING    warning conditions
 *                     LOG_NOTICE     normal, but significant, condition
 *                     LOG_INFO       informational message
 *                     LOG_DEBUG      debug-level message
 *   format: printf()-style format specifier
 *   ...   : arguments to use in expansion of 'format'.
 */
void verbose_log(unsigned    verbosity_level,
                 int         log_level,
                 const char *format,
                 ...)
    __attribute__((format(printf, 3, 4)));

void verbose_initialize(const char *program_name);
void verbose_finalize(void);
#endif
