/* Copyright (c) 2011 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
#include <assert.h>
#include <limits.h>

#include "board.h"
#include "verbose.h"
#include "initialization.h"

#define WEAK __attribute__((weak))

/* [__start_initialization, __stop_initialization) is an array
 * of pointers which reference the actual initializers.
 *
 * If no initializers are defined, both symbols will be NULL; be sure
 * to take this detail into consideration when traversing the set of
 * initializers.
 */
extern initialization_descriptor_t WEAK *__start_initialization_descriptors;
extern initialization_descriptor_t WEAK *__stop_initialization_descriptors;

static initialization_descriptor_t **initialization_descriptor_start(void)
{
    return &__start_initialization_descriptors;
}

static initialization_descriptor_t **initialization_descriptor_stop(void)
{
    return &__stop_initialization_descriptors;
}

#define FOREACH_INITIALIZER(_body)                                      \
    {                                                                   \
        initialization_descriptor_t **_beg =                            \
            initialization_descriptor_start();                          \
        initialization_descriptor_t **_end =                            \
            initialization_descriptor_stop();                           \
        while (_beg < _end) {                                           \
            /* 'desc' variable is available for use in '_body' */       \
            initialization_descriptor_t *desc = *_beg;                  \
            verbose_log(1, LOG_INFO, "%s: '%s'",                        \
                        __FUNCTION__, desc->name);                      \
            _body;                                                      \
            ++_beg;                                                     \
        }                                                               \
    }

void initialization_initialize(void)
{
    FOREACH_INITIALIZER({
            desc->initialize();
        });
}
void initialization_finalize(void)
{
    FOREACH_INITIALIZER({
            desc->finalize();
        });
}
