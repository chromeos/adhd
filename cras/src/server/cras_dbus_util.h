/* Copyright 2013 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef CRAS_SRC_SERVER_CRAS_DBUS_UTIL_H_
#define CRAS_SRC_SERVER_CRAS_DBUS_UTIL_H_

#include <dbus/dbus.h>
#include <stdbool.h>

/* Appends a key-value pair to the dbus message.
 * Args:
 *    key - The key (a string).
 *    type - The type of the value (for example, 'y').
 *    type_string - The type of the value in string form (for example, "y").
 *    value - A pointer to the value to be appended.
 * Returns:
 *    False if not enough memory.
 */
dbus_bool_t append_key_value(DBusMessageIter* iter,
                             const char* key,
                             int type,
                             const char* type_string,
                             void* value);

/* Extract a single argument from a DBus message.
 * Args:
 *    message - The DBus reply message to parse argument from.
 *    dbus_type - The DBus type of the argument to parse.
 *    arg - Pointer to store the parsed value.
 * Returns:
 *    0 on success, otherwise a negative errno.
 */
int get_single_arg(DBusMessage* message, int dbus_type, void* arg);

/* Create a DBusMessage for a method call with basic type arguments.
 * Args:
 *    method_call - The location to store the pointer of the created message.
 *    dest - Name of the message receiver.
 *    path - The object path for the method.
 *    iface - Interface to invoke the method on.
 *    method_name - Name of the method.
 *    num_args - Number of arguments to the method.
 *    ... - Variable number of arguments to the method.
 * Returns:
 *    0 on success, otherwise a negative errno.
 */
int create_dbus_method_call(DBusMessage** method_call,
                            const char* dest,
                            const char* path,
                            const char* iface,
                            const char* method_name,
                            int num_args,
                            ...);

/* Calls a method on the specificed DBus connection and parses the reply.
 * Args:
 *    conn - The DBus connection to use for the method call.
 *    method_call - The DBus method call to invoke.
 *    dbus_ret_type - The expected DBus type of the return value.
 *                    Set to DBUS_TYPE_INVALID to skip parsing.
 *    dbus_ret_value_ptr - Pointer to store the parsed return value.
 * Returns:
 *    0 on success, otherwise a negative errno.
 */
int call_method_and_parse_reply(DBusConnection* conn,
                                DBusMessage* method_call,
                                int dbus_ret_type,
                                void* dbus_ret_value_ptr);

/* Repeatedly calls a method on a DBus connection until the specified predicate
 * is satisfied or a maximum number of retries is reached.
 * Args:
 *    conn - The DBus connection to use for the method call.
 *    num_retries - The maximum number of times to retry the method call.
 *    sleep_time_us - The time to sleep between retries, in microseconds.
 *    method_call - The DBus method call to invoke.
 *    dbus_ret_type - The expected DBus type of the return value.
 *                    Set to DBUS_TYPE_INVALID to skip parsing.
 *    dbus_ret_value_ptr - Pointer to store the parsed return value.
 *    predicate - The predicate function to use for determining success.
 * Returns:
 *    0 on success, otherwise a negative errno.
 */
int retry_until_predicate_satisfied(struct DBusConnection* conn,
                                    int num_retries,
                                    int sleep_time_us,
                                    DBusMessage* method_call,
                                    int dbus_ret_type,
                                    void* dbus_ret_value_ptr,
                                    bool (*predicate)(int, void*));

#endif
