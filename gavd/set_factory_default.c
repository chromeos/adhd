/* Copyright (c) 2011 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
#include <assert.h>
#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>

#include "board.h"
#include "adhd_alsa_defs.h"
#include "codec.h"
#include "utils.h"
#include "verbose.h"
#include "workfifo.h"
#include "thread_management.h"
#include "set_factory_default.h"

#define ASOUND_STATE "/etc/asound.state"

FIFO_ENTRY("Set Internal Factory Default", workfifo, set_factory_default,
{
    char        cmd_buf[128];
    size_t      card_number = (size_t)data;
    struct stat stat_buf;
    int         r;

    VERBOSE_FUNCTION_ENTER("%p", data);

    if (stat(ASOUND_STATE, &stat_buf) == 0) {
        threads_lock_hardware();
        verbose_log(0, LOG_INFO, "%s: initialize card '%zu' to factory default",
                    __FUNCTION__, card_number);
        r = snprintf(cmd_buf, sizeof(cmd_buf) / sizeof(cmd_buf[0]),
                     ADHD_ALSACTL_COMMAND " --file "
                     ASOUND_STATE " restore %zu", card_number);
        assert((size_t)r < sizeof(cmd_buf) / sizeof(cmd_buf[0]));
        cmd_buf[sizeof(cmd_buf) / sizeof(cmd_buf[0]) - 1] = '\0';
        utils_execute_command(cmd_buf);
        threads_unlock_hardware();
    }
    VERBOSE_FUNCTION_EXIT("%p", data);
 });

void factory_default_add_event(size_t card_number)
{
    FIFO_ADD_ITEM(workfifo, set_factory_default, (void *)card_number);
}
