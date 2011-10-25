/* Copyright (c) 2011 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
#if !defined(_INITIALIZATION_H_)
#define _INITIALIZATION_H_
#include "linkerset.h"

typedef void (*initialization_fn_t)(void);

typedef struct initialization_descriptor_t {
    initialization_fn_t  id_initialize;
    initialization_fn_t  id_finalize;
    const char          *id_name;
} initialization_descriptor_t;

/* inv: There can be no ordering dependencies between initializers. */
#define INITIALIZER(_name, _initialize, _finalize)                      \
    static const initialization_descriptor_t                            \
    __initialization_descriptor_##_fn = {                               \
        .id_name       = _name,                                         \
        .id_initialize = _initialize,                                   \
        .id_finalize   = _finalize,                                     \
    };                                                                  \
    LINKERSET_ADD_ITEM(initialization_descriptor,                       \
                       __initialization_descriptor_##_fn)

void initialization_initialize(void);
void initialization_finalize(void);
#endif
