
/* Copyright (c) 2011 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
#include <assert.h>
#include <stdlib.h>

#include "board.h"

#if defined(ADHD_SET_FACTORY_DEFAULT)
#include "verbose.h"
#include "initialization.h"

static void set_factory_default(void)
{
    char const * const cmd = "alsactl --file /etc/asound.state restore";
    int result;

    VERBOSE_FUNCTION_ENTER();
    result = system(cmd);
    if (result == 0) {
        verbose_log(0, LOG_WARNING, "%s: '%s' succeeded.", __FUNCTION__, cmd);
    } else if (result == -1) {
        verbose_log(0, LOG_WARNING,
                    "%s: Unable to invoke '%s'.", __FUNCTION__, cmd);
    } else if (result != 0) {
        verbose_log(0, LOG_WARNING,
                    "%s: '%s' failed.  Return code: %d.",
                    __FUNCTION__, cmd, result);
    }
    VERBOSE_FUNCTION_EXIT();
}

INITIALIZER("Set Factory Default",
            set_factory_default,
            set_factory_default);
#endif
