/* Copyright (c) 2011 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
#include <assert.h>

#include "board.h"

#include "utils.h"
#include "verbose.h"
#include "fifo.h"
#include "thread_management.h"
#include "set_factory_default.h"

WORKFIFO_ENTRY("Set Internal Factory Default", set_factory_default,
{
    VERBOSE_FUNCTION_ENTER("%p", data);
    if (adhd_initialize_sound_command) {
        /* TODO(thutt): Stop gap only until /etc/asound.rc is loaded
         *              on login.
         *
         * Warning: Do not put this in a thread.
         *          Adding other Alsa-based threads before
         *          /etc/asound.rc is loaded will cause race
         *          conditions with sound initialization.
         */
        threads_lock_hardware();
        utils_execute_command(ADHD_INITIALIZE_SOUND_COMMAND);
        threads_unlock_hardware();
    } else if (adhd_set_factory_default) {
        threads_lock_hardware();
        utils_execute_command(ADHD_ALSACTL_COMMAND
                              " --file /etc/asound.state restore");
        threads_unlock_hardware();
    }
    VERBOSE_FUNCTION_EXIT("%p", data);
 });

void factory_default_add_event(void)
{
    workfifo_add_item(WORKFIFO_ENTRY_ID(set_factory_default), NULL);
}
