
/* Copyright (c) 2011 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
#include <assert.h>
#include <unistd.h>

#include "board.h"

#if defined(ADHD_GPIO_HEADPHONE)
#include "verbose.h"
#include "thread_management.h"


static void *gpio_headphone(void *arg)
{
    verbose_log(5, LOG_INFO, "%s: start", __FUNCTION__);
    arg = arg;                  /* Silence compiler warning. */

    while (!thread_management.exit) {
        sleep(1);
    }
    verbose_log(5, LOG_INFO, "%s exited", __FUNCTION__);
    return NULL;
}

THREAD_DESCRIPTOR("GPIO Headphone", gpio_headphone);
#endif
