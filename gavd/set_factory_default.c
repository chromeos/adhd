
/* Copyright (c) 2011 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
#include <assert.h>

#include "board.h"

#if defined(ADHD_SET_FACTORY_DEFAULT)
#include "utils.h"
#include "verbose.h"
#include "initialization.h"

static void set_factory_default(void)
{
    char const * const cmd = "alsactl --file /etc/asound.state restore";

    VERBOSE_FUNCTION_ENTER();
    utils_execute_command(cmd);
    VERBOSE_FUNCTION_EXIT();
}

INITIALIZER("Set Factory Default",
            set_factory_default,
            set_factory_default);
#endif
