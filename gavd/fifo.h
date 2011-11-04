/* Copyright (c) 2011 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
#if !defined(_FIFO_H_)
#define _FIFO_H_
#include "stdmacro.h"
#include "linkerset.h"

/* fifo_entry_t: FIFO entry handler information
 *
 *  This type is the primary data which is placed into a FIFO.
 *
 * inv: fe_handler != NULL
 *      fe_handler is a function which is called to 'handle' the FIFO element.
 *
 * inv: fe_name    != NULL
 *      fe_name is a string which identifies the fifo_entry_t.
 *      The names need not be unique; uniqueness is guaranteed by the
 *      fact that each 'fifo_entry_t' has a unique address.
 */
typedef struct fifo_entry_t {
    void       (*fe_handler)(void *);
    const char *fe_name;
} fifo_entry_t;

#define __FIFO_ENTRY_ID(_id)                \
    XCONCAT(__fifo_descriptor_, _id)

#define FIFO_ENTRY_ID(_id)                  \
    &__FIFO_ENTRY_ID(_id)

/* __FIFO_ENTRY_FUNCTION: Creates function name for FIFO entry handler.
 *
 * Internal use only.
 */
#define __FIFO_ENTRY_FUNCTION(_id)            \
    XCONCAT(fifo_handler_, _id)

/* FIFO_ENTRY: Describes a handler for a FIFO entry.
 *
 *  _name   : String literal name which describes the handler.
 *  _id     : The name of the handler.
 *            This must be a valid C identifier, and it is used
 *            to create a function name for the FIFO element
 *            handler code.
 *  _handler: Code used to handle the FIFO element.
 */
#define FIFO_ENTRY(_name, _id, _handler)                        \
    static void __FIFO_ENTRY_FUNCTION(_id)(void *data)          \
    {                                                           \
        _handler;                                               \
    }                                                           \
    static const fifo_entry_t                                   \
    __FIFO_ENTRY_ID(_id) = {                                    \
        .fe_name     = _name,                                   \
        .fe_handler  = __FIFO_ENTRY_FUNCTION(_id),              \
    };                                                          \
    LINKERSET_ADD_ITEM(fifo_entry, __FIFO_ENTRY_ID(_id))

/* fifo_add_item: Add an item to the end of the work fifo
 *
 *  result == 1 -> Successfully added.
 *  result == 0 -> Entry not added.
 *
 *  result == 1 -> 'data' is managed (i.e., free()) by 'handler'.
 *  result == 0 -> 'data' is managed (i.e., free()) by the caller
 *                 of this function.
 */
unsigned fifo_add_item(const fifo_entry_t *handler, void *data);
#endif
