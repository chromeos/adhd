/* Copyright (c) 2011 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
#if !defined(_INITIALIZATION_H_)
#define _INITIALIZATION_H_

typedef void (*initialization_fn_t)(void);

typedef struct initialization_descriptor_t {
    initialization_fn_t  initialize;
    initialization_fn_t  finalize;
    const char          *name;
} initialization_descriptor_t;

/* inv: There can be no ordering dependencies between initializers. */
#define INITIALIZER(_name, _initialize, _finalize)                      \
    static const initialization_descriptor_t                            \
    __initialization_descriptor_##_fn = {                               \
        .name       = _name,                                            \
        .initialize = _initialize,                                      \
        .finalize   = _finalize,                                        \
    };                                                                  \
    __asm__(".global __start_initialization_descriptors");              \
    __asm__(".global __stop_initialization_descriptors");               \
    static void const * const                                           \
    __initialization_descriptor_ptr_##_fn                               \
    __attribute__((section("initialization_descriptors"),used)) =       \
         &__initialization_descriptor_##_fn

void initialization_initialize(void);
void initialization_finalize(void);
#endif
