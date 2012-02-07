/* Copyright (c) 2011 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
#include <assert.h>

#include "board.h"
#include "adhd_alsa_defs.h"
#include "codec.h"
#include "utils.h"
#include "verbose.h"
#include "workfifo.h"
#include "thread_management.h"
#include "set_factory_default.h"

FIFO_ENTRY("Set Internal Factory Default", workfifo, set_factory_default,
{
    VERBOSE_FUNCTION_ENTER("%p", data);
    if (1) {
        /* TODO(thutt): Stop gap only until /etc/asound.rc is loaded
         *              on login.
         *
         * Warning: Do not put this in a thread.
         *          Adding other Alsa-based threads before
         *          /etc/asound.rc is loaded will cause race
         *          conditions with sound initialization.
         */
        threads_lock_hardware();
        codec_initialize_hardware();
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
    FIFO_ADD_ITEM(workfifo, set_factory_default, NULL);
}
