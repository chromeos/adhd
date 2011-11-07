/* Copyright (c) 2011 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
#include <stdlib.h>

#include "verbose.h"
#include "utils.h"

unsigned utils_execute_command(const char *cmd)
{
    int result = system(cmd);

    if (result == 0) {
        verbose_log(7, LOG_WARNING, "%s: '%s' succeeded.",
                    __FUNCTION__, cmd);
        return 1;
    } else {
        if (result == -1) {
            verbose_log(0, LOG_WARNING,
                        "%s: Unable to invoke '%s'.", __FUNCTION__, cmd);
        } else if (result != 0) {
            verbose_log(0, LOG_WARNING,
                        "%s: '%s' failed.  Return code: %d.",
                        __FUNCTION__, cmd, result);
        }
        return 0;
    }
}
