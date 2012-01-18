/* Copyright (c) 2011, 2012 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * This module implements a FIFO that is used as a worklist.  All work
 * performed by gavd will flow through this worklist, and be processed
 * in order of arrival.
 *
 * The FIFO is implemented, for simplicity, as a doubly-linked list
 * with a dummy head.  The dummy head removes any special cases
 * associated with an empty FIFO.
 */
#include <assert.h>
#include <stdlib.h>
#include <dbus/dbus.h>

#include "verbose.h"
#include "thread_management.h"
#include "initialization.h"
#include "dbus_connection.h"

static DBusConnection *dbus_bus_connection;
static const char     *dbus_connection_name = "org.chromium.gavd";


void dbus_connection_jack_state(const char *jack, unsigned state)
{
    dbus_uint32_t  serial = 0;
    DBusMessage   *msg;

    assert(state < 2);          /* 0 => unplugged, 1 => plugged */
    msg = dbus_message_new_signal("/gavd/jack", "gavd.jack.state", "jack");

    if (msg != NULL) {
        DBusMessageIter args;
        dbus_bool_t     jack_append;
        dbus_bool_t     state_append;

        dbus_message_iter_init_append(msg, &args);
        jack_append  = dbus_message_iter_append_basic(&args,
                                                     DBUS_TYPE_STRING,
                                                      &jack);
        state_append = dbus_message_iter_append_basic(&args,
                                                      DBUS_TYPE_BOOLEAN,
                                                      &state);

        if (jack_append && state_append) {
            if (!dbus_connection_send(dbus_bus_connection, msg, &serial)) {
                verbose_log(0, LOG_ERR, "%s: out of memory: message send",
                        __FUNCTION__);
            }
            dbus_connection_flush(dbus_bus_connection);
        } else {
            verbose_log(0, LOG_ERR, "%s: out of memory: argument append",
                        __FUNCTION__);
        }
        dbus_message_unref(msg);
    } else {
        verbose_log(0, LOG_ERR, "%s: out of memory", __FUNCTION__);
    }
}

void dbus_connection_device_state(unsigned    action,
                                  const char *udev_sysname,
                                  unsigned    card_number,
                                  unsigned    device_number,
                                  unsigned    active,
                                  unsigned    internal,
                                  unsigned    primary)
{
    const char  *mode;
    DBusMessage *msg;

    switch (action) {
    case 0:  mode = "add";       break;
    case 1:  mode = "remove";    break;
    case 2:  mode = "change";    break;
    default: mode = "<invalid>"; break;
    }

    msg = dbus_message_new_signal("/gavd/alsa", "gavd.alsa.card", mode);
    if (msg != NULL) {
        dbus_uint32_t   serial = 0;
        dbus_uint32_t   bits;
        DBusMessageIter args;

        bits = (((card_number   & 0xffU) << 24) | /* bits 31..24 */
                ((device_number & 0xffU) << 16) | /* bits 23..16 */
                ((active   & 1)          << 15) | /* bit      15 */
                ((internal & 1)          << 14) | /* bit      14 */
                ((primary  & 1)          << 13) | /* bit      13 */
                ((action   & 2)          <<  0)); /* bits 01..00 */

        dbus_message_iter_init_append(msg, &args);
        if (dbus_message_iter_append_basic(&args, DBUS_TYPE_STRING,
                                           &udev_sysname)           &&
            dbus_message_iter_append_basic(&args, DBUS_TYPE_UINT32,
                                           &bits)) {
            if (!dbus_connection_send(dbus_bus_connection, msg, &serial)) {
                verbose_log(0, LOG_ERR, "%s: out of memory: message send",
                        __FUNCTION__);
            }
            dbus_connection_flush(dbus_bus_connection);
        } else {
            verbose_log(0, LOG_ERR, "%s: out of memory: argument append",
                        __FUNCTION__);
        }
        dbus_message_unref(msg);
    } else {
        verbose_log(0, LOG_ERR, "%s: out of memory: argument append",
                    __FUNCTION__);
    }
}

static void initialize(void)
{
    DBusError error;
    int       retval;

    dbus_error_init(&error);
    dbus_bus_connection = dbus_bus_get(DBUS_BUS_SYSTEM, &error);
    if (dbus_bus_connection != NULL) {
        dbus_error_free(&error);
        dbus_error_init(&error);

        /* Request name on bus */
        retval = dbus_bus_request_name(dbus_bus_connection,
                                       dbus_connection_name,
                                       DBUS_NAME_FLAG_REPLACE_EXISTING,
                                       &error);
        if (dbus_error_is_set(&error)) {
            verbose_log(0, LOG_ERR, "%s: name error (%s)",
                        __FUNCTION__, error.message);
            dbus_error_free(&error);
        }
        if (DBUS_REQUEST_NAME_REPLY_PRIMARY_OWNER != retval) {
            verbose_log(0, LOG_ERR, "%s: not primary owner of connection",
                        __FUNCTION__);
            threads_quit_daemon();
        }
    } else {
        verbose_log(0, LOG_ERR, "%s: unable to initialize dbus",
                    __FUNCTION__);
        threads_quit_daemon();
    }
    dbus_error_free(&error);
}

static void finalize(void)
{
    DBusError error;

    dbus_error_init(&error);
    dbus_bus_release_name(dbus_bus_connection, dbus_connection_name, &error);
    dbus_error_free(&error);
}

INITIALIZER("GAVD dBus Controller", initialize, finalize);
