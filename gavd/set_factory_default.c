
/* Copyright (c) 2011 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
#include <assert.h>

#include "board.h"

#if defined(ADHD_SET_FACTORY_DEFAULT) || \
    defined(ADHD_INITIALIZE_SOUND_COMMAND)
#include "utils.h"
#include "verbose.h"
#include "initialization.h"

static void set_factory_default(void)
{
    VERBOSE_FUNCTION_ENTER("%s", "void")
    if (adhd_initialize_sound_command) {
        /* TODO(thutt): Stop gap only until /etc/asound.rc is loaded
         *              on login.
         *
         * Warning: Do not put this in a thread.
         *          Adding other Alsa-based threads before
         *          /etc/asound.rc is loaded will cause race
         *          conditions with sound initialization.
         */
        utils_execute_command(ADHD_INITIALIZE_SOUND_COMMAND);
    } else {
        char const * const cmd = "alsactl --file /etc/asound.state restore";
        utils_execute_command(cmd);
    }
    VERBOSE_FUNCTION_EXIT("%s", "void");
}

INITIALIZER("Set Factory Default",
            set_factory_default,
            set_factory_default);
#endif
