/* Copyright (c) 2011 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
#if !defined(_FIFO_H_)
#define _FIFO_H_
#include <unistd.h>

#include "pthread.h"
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

/* fifo_node_t: A node which is stored in the fifo.
 *
 * inv: fn_prev  != NULL
 * inv: fn_next  != NULL
 * inv: fn_entry != NULL
 * inv: fn_data is not managed by the FIFO system.
 *      It is the responsibility of the code which added the element
 *      to the FIFO, or the handler described by 'fn_entry' to manage
 *      any resources associated with 'fn_data'.
 */
typedef struct fifo_node_t fifo_node_t;
struct fifo_node_t {
    fifo_node_t        *fn_prev;
    fifo_node_t        *fn_next;
    const fifo_entry_t *fn_entry;
    void               *fn_data;
};

/* fifo_t: generic FIFO descriptor.
 *
 * A FIFO is implemented using a mutex and an doubly linked list with
 * a dummy head node.  The mutex is used to ensure that the data
 * structure is not corrupted via simultaneous access.
 *
 * Create: fifo_create()
 * Destroy: fifo_destroy()
 *
 * inv: fifo_head != NULL
 * inv: fifo empty -> fifo_head->fn_next == fifo_head
 * inv: fifo empty -> fifo_head->fn_next == fifo->fn_prev
 */
typedef struct fifo_t {
    fifo_node_t     *fifo_head;
    pthread_mutex_t  fifo_mutex;
} fifo_t;

/* FIFO_ENTRY_NAME(<fifo name>)
 *
 * Creates an identifier based on the name of the FIFO.  This name is
 * used to create a unique 'FIFO entry type', and this unique type is
 * used to ensure that different FIFO implementations cannot have
 * incorrect elements inserted into them.
 */
#define FIFO_ENTRY_NAME(_type)                  \
    XCONCAT(fifo_entry_, _type)

/* FIFO_ENTRY_TYPE(<fifo name>): Create a typename for a FIFO */
#define FIFO_ENTRY_TYPE(_type) \
    XCONCAT(FIFO_ENTRY_NAME(_type), _t)

/* FIFO_DECLARE(<fifo name>): Declare types & variables for a FIFO.
 *
 *  This macro creates a unique type and external function reference
 *  for a FIFO named '<fifo name>'.
 *
 * See FIFO_DEFINE.
 */
#define FIFO_DECLARE(_name)                                             \
    typedef struct FIFO_ENTRY_TYPE(_name) {                             \
        fifo_entry_t entry;                                             \
    } FIFO_ENTRY_TYPE(_name);                                           \
    extern fifo_t *_name;                                               \
    extern unsigned XCONCAT(fifo_add_item_, _name)(fifo_t *fifo,        \
                                  const FIFO_ENTRY_TYPE(_name) *entry,  \
                                  void *data)

/* FIFO_ADD_ITEM: Add an item to the named FIFO.
 *
 * _name : The name of the fifo.  See FIFO_DEFINE, FIFO_DECLARE
 * _entry: The name of the FIFO entry handler.  See FIFO_ENTRY.
 * _data : The data which is passed as an argument to '_entry'.
 */
#define FIFO_ADD_ITEM(_name, _entry, _data)                             \
    XCONCAT(fifo_add_item_, _name)(_name,                               \
                                  __FIFO_DESCRIPTOR_ADDRESS(_name,      \
                                                            _entry),    \
                                  _data)

/* FIFO_DEFINE(<fifo name>): Define types & variables for a FIFO.
 *
 * See FIFO_DECLARE.
 */
#define FIFO_DEFINE(_name)                                              \
    LINKERSET_DECLARE(XCONCAT(fifo_entry_, _name));                     \
    fifo_t *_name;                                                      \
    unsigned XCONCAT(fifo_add_item_, _name)(fifo_t *fifo,               \
                           const FIFO_ENTRY_TYPE(_name) *entry,         \
                           void *data)                                  \
    {                                                                   \
        return __fifo_add_item(fifo, &entry->entry, data);              \
    }

/* __FIFO_DESCRIPTOR_ID: Creates the name of the internal FIFO descriptor.
 *
 *  Internal use only.
 *
 *  _fifo: The name of the FIFO.  See FIFO_DECLARE().
 *  _id  : The name of the FIFO entry.  See FIFO_ENTRY().
 */
#define __FIFO_DESCRIPTOR_ID(_fifo, _id)                                \
    XCONCAT(__fifo_descriptor_, XCONCAT(_fifo, XCONCAT(_, _id)))

/*  __FIFO_DESCRIPTOR_ADDRESS: Obtains address of the descrbied FIFO
 *                             descriptor.
 *
 *  Internal use only.
 *
 *  _fifo: The name of the FIFO.  See FIFO_DECLARE().
 *  _id  : The name of the FIFO entry.  See FIFO_ENTRY().
 */
#define __FIFO_DESCRIPTOR_ADDRESS(_fifo, _id)     \
    &__FIFO_DESCRIPTOR_ID(_fifo, _id)

/* __FIFO_ENTRY_FUNCTION: Creates function name for FIFO entry handler.
 *
 * Internal use only.
 */
#define __FIFO_ENTRY_FUNCTION(_fifo, _id)         \
    XCONCAT(fifo_handler_, XCONCAT(_fifo, XCONCAT(_, _id)))

/* FIFO_ENTRY: Describes a handler for a FIFO entry.
 *
 *  _name   : String literal name which describes the handler.
 *  _fifo   : The name of the fifo.  See FIFO_DECLARE().
 *  _id     : The name of the handler.
 *            This must be a valid C identifier, and it is used
 *            to create a function name for the FIFO element
 *            handler code.
 *  _handler: Code used to handle the FIFO element.
 */
#define FIFO_ENTRY(_name, _fifo, _id, _handler)                 \
    static void __FIFO_ENTRY_FUNCTION(_fifo, _id)(void *data)   \
    {                                                           \
        _handler;                                               \
    }                                                           \
    static const FIFO_ENTRY_TYPE(_fifo)                         \
    __FIFO_DESCRIPTOR_ID(_fifo, _id) = {                        \
        .entry = {                                              \
            .fe_name     = _name,                               \
            .fe_handler  = __FIFO_ENTRY_FUNCTION(_fifo, _id),   \
        },                                                      \
    };                                                          \
    LINKERSET_ADD_ITEM(FIFO_ENTRY_NAME(_fifo),                  \
                       __FIFO_DESCRIPTOR_ID(_fifo, _id))

/* FIFO_EVENT_ITERATE: Iterate over all FIFO elements
 *
 * _fifo: The name of the FIFO.  See FIFO_DECLARE().
 * _var : Locally created variable name.  Use in '_code'.
 * _code: Code to be executed for each FIFO element type.
 */
#define FIFO_ELEMENT_ITERATE(_fifo, _var, _code)                  \
    LINKERSET_ITERATE(FIFO_ENTRY_NAME(_fifo), _var, _code);

/* __fifo_add_item: Add an item to the end of the work fifo.
 *                  INTERNAL USE ONLY.
 *
 *  result == 1 -> Successfully added.
 *  result == 0 -> Entry not added.
 *
 *  result == 1 -> 'data' is managed (i.e., free()) by 'handler'.
 *  result == 0 -> 'data' is managed (i.e., free()) by the caller
 *                 of this function.
 */
unsigned __fifo_add_item(fifo_t             *fifo,
                         const fifo_entry_t *handler,
                         void               *data);

/* fifo_monitor_work: Function which monitors the fifo, and
 *                    dispatches to the handler for each element.
 *
 * thread_name: The name of the thread running this function.
 * fifo       : The FIFO to be monitored.
 * sleep_usec : Time to wait between checking if FIFO is not empty.
 */
void fifo_monitor_work(const char *thread_name,
                       fifo_t     *fifo,
                       useconds_t  sleep_usec);

fifo_t *fifo_create(void);
void fifo_destroy(fifo_t *fifo);
#endif
