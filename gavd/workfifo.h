/* Copyright (c) 2011 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
#if !defined(_WORKFIFO_H_)
#define _WORKFIFO_H_
#include "stdmacro.h"
#include "linkerset.h"

typedef struct workfifo_entry_t {
    void       (*wf_handler)(void *);
    const char *wf_name;
} workfifo_entry_t;

#define __WORKFIFO_ENTRY_ID(_id)                \
    XCONCAT(__workfifo_descriptor_, _id)

#define WORKFIFO_ENTRY_ID(_id)                  \
    &__WORKFIFO_ENTRY_ID(_id)

#define WORKFIFO_ENTRY_FUNCTION(_id)            \
    XCONCAT(workfifo_handler_, _id)

/*  _name   : String literal name for the fifo entry.
 *  _handler: Code used to handle the FIFO data.
 */
#define WORKFIFO_ENTRY(_name, _id, _handler)                    \
    static void WORKFIFO_ENTRY_FUNCTION(_id)(void *data)        \
    {                                                           \
        _handler;                                               \
    }                                                           \
    static const workfifo_entry_t                               \
    __WORKFIFO_ENTRY_ID(_id) = {                                \
        .wf_name     = _name,                                   \
        .wf_handler  = WORKFIFO_ENTRY_FUNCTION(_id),            \
    };                                                          \
    LINKERSET_ADD_ITEM(workfifo_entry, __WORKFIFO_ENTRY_ID(_id))

void workfifo_add_item(const workfifo_entry_t *handler, void *data);
#endif
