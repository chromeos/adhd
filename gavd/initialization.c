/* Copyright (c) 2011 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
#include <assert.h>
#include <limits.h>

#include "board.h"
#include "verbose.h"
#include "linkerset.h"
#include "initialization.h"

#define WEAK __attribute__((weak))

LINKERSET_DECLARE(initialization_descriptor);

void initialization_initialize(void)
{
    LINKERSET_ITERATE(initialization_descriptor, desc,
                      {
                          verbose_log(1, LOG_INFO, "%s: '%s'",
                                      __FUNCTION__, desc->id_name);
                          desc->id_initialize();
                      });
}
void initialization_finalize(void)
{
    LINKERSET_ITERATE(initialization_descriptor, desc,
                      {
                          verbose_log(1, LOG_INFO, "%s: '%s'",
                                      __FUNCTION__, desc->id_name);
                          desc->id_finalize();
                      });
}
