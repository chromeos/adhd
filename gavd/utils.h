/* Copyright (c) 2011 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
#if !defined(_ADHD_UTILS_H_)
#define _ADHD_UTILS_H_

/* utils_execute_command: Execute a command using 'system()'.
 *
 * Returns '1' if execution successfull.
 * Returns '0' is execution failed.
 *
 * Logs at verbosity '0' the status of the command executed.
 */
unsigned utils_execute_command(const char *cmd);
#endif
