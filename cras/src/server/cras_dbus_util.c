/* Copyright 2013 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "cras/src/server/cras_dbus_util.h"

#include <errno.h>
#include <stdbool.h>
#include <syslog.h>
#include <unistd.h>

dbus_bool_t append_key_value(DBusMessageIter* iter,
                             const char* key,
                             int type,
                             const char* type_string,
                             void* value) {
  DBusMessageIter entry, variant;

  if (!dbus_message_iter_open_container(iter, DBUS_TYPE_DICT_ENTRY, NULL,
                                        &entry)) {
    return FALSE;
  }
  if (!dbus_message_iter_append_basic(&entry, DBUS_TYPE_STRING, &key)) {
    return FALSE;
  }
  if (!dbus_message_iter_open_container(&entry, DBUS_TYPE_VARIANT, type_string,
                                        &variant)) {
    return FALSE;
  }
  if (!dbus_message_iter_append_basic(&variant, type, value)) {
    return FALSE;
  }
  if (!dbus_message_iter_close_container(&entry, &variant)) {
    return FALSE;
  }
  if (!dbus_message_iter_close_container(iter, &entry)) {
    return FALSE;
  }

  return TRUE;
}

int get_single_arg(DBusMessage* message, int dbus_type, void* arg) {
  DBusError dbus_error;

  dbus_error_init(&dbus_error);

  if (!dbus_message_get_args(message, &dbus_error, dbus_type, arg,
                             DBUS_TYPE_INVALID)) {
    syslog(LOG_WARNING, "Bad method received: %s", dbus_error.message);
    dbus_error_free(&dbus_error);
    return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
  }

  return 0;
}

int create_dbus_method_call(DBusMessage** method_call,
                            const char* dest,
                            const char* path,
                            const char* iface,
                            const char* method_name,
                            int num_args,
                            ...) {
  *method_call = dbus_message_new_method_call(dest, path, iface, method_name);

  if (!*method_call) {
    syslog(LOG_ERR, "%s: cannot create message due to OOM.", __func__);
    return -ENOMEM;
  }

  DBusMessageIter iter;
  dbus_message_iter_init_append(*method_call, &iter);

  va_list args;
  va_start(args, num_args);
  for (int i = 0; i < num_args; ++i) {
    int type = va_arg(args, int);
    void* arg = va_arg(args, void*);
    if (!dbus_message_iter_append_basic(&iter, type, arg)) {
      syslog(LOG_ERR, "%s: cannot append args to message due to OOM.",
             __func__);
      dbus_message_unref(*method_call);
      va_end(args);
      return -ENOMEM;
    }
  }
  va_end(args);

  return 0;
}

int call_method_and_parse_reply(DBusConnection* conn,
                                DBusMessage* method_call,
                                int dbus_ret_type,
                                void* dbus_ret_value_ptr) {
  int rc = 0;
  const char* method_name = dbus_message_get_member(method_call);

  DBusError dbus_error;
  dbus_error_init(&dbus_error);

  DBusMessage* reply = dbus_connection_send_with_reply_and_block(
      conn, method_call, DBUS_TIMEOUT_USE_DEFAULT, &dbus_error);

  if (!reply) {
    syslog(LOG_ERR, "Failed to send %s : %s", method_name, dbus_error.message);
    rc = -EIO;
    goto cleanup;
  }

  if (dbus_message_get_type(reply) == DBUS_MESSAGE_TYPE_ERROR) {
    syslog(LOG_ERR, "%s returned error: %s", method_name,
           dbus_message_get_error_name(reply));
    rc = -EIO;
    goto cleanup;
  }

  // In this case we don't care about the return value.
  if (dbus_ret_type == DBUS_TYPE_INVALID) {
    goto cleanup;
  }

  if (get_single_arg(reply, dbus_ret_type, dbus_ret_value_ptr) != 0) {
    rc = -EIO;
    goto cleanup;
  }

cleanup:
  dbus_error_free(&dbus_error);
  if (reply) {
    dbus_message_unref(reply);
  }

  return rc;
}

int retry_until_predicate_satisfied(struct DBusConnection* conn,
                                    int num_retries,
                                    int sleep_time_us,
                                    DBusMessage* method_call,
                                    int dbus_ret_type,
                                    void* dbus_ret_value_ptr,
                                    bool (*predicate)(int, void*)) {
  const char* method_name = dbus_message_get_member(method_call);

  syslog(LOG_DEBUG, "%s: polling until started", method_name);

  for (int retry = 0; retry < num_retries; ++retry) {
    int rc = call_method_and_parse_reply(
        /* conn= */ conn,
        /* method_call= */ method_call,
        /* dbus_ret_type= */ dbus_ret_type,
        /* dbus_ret_value_ptr= */ dbus_ret_value_ptr);

    // Some fundamental error occured, abort immediately.
    if (rc < 0) {
      return rc;
    }

    if (predicate(dbus_ret_type, dbus_ret_value_ptr)) {
      return 0;
    }

    usleep(sleep_time_us);
  }

  syslog(LOG_ERR, "%s: polling failed after %d us", method_name,
         num_retries * sleep_time_us);

  return -EBUSY;
}
