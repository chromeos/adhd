/* Copyright 2021 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <dbus/dbus.h>
#include <errno.h>
#include <stdbool.h>
#include <string.h>
#include <syslog.h>

#include "cras/src/common/cras_string.h"
#include "cras/src/server/cras_bt_constants.h"
#include "cras/src/server/cras_bt_log.h"
#include "cras/src/server/cras_bt_manager.h"
#include "cras/src/server/cras_bt_policy.h"
#include "cras/src/server/cras_fl_media.h"

#define BT_MANAGER_SERVICE_NAME "org.chromium.bluetooth.Manager"
#define BT_MANAGER_INTERFACE "org.chromium.bluetooth.Manager"
#define BT_MANAGER_OBJECT "/org/chromium/bluetooth/Manager"
#define BT_CALLBACK_INTERFACE "org.chromium.bluetooth.ManagerCallback"

#define CRAS_BT_OBJECT_PATH "/org/chromium/cras/bluetooth"

enum BtState {
  BT_STATE_OFF,
  BT_STATE_TURNING_ON,
  BT_STATE_ON,
  BT_STATE_TURNING_OFF
};

static void floss_manager_on_register_callback(DBusPendingCall* pending_call,
                                               void* data) {
  DBusMessage* reply;

  reply = dbus_pending_call_steal_reply(pending_call);
  dbus_pending_call_unref(pending_call);

  if (dbus_message_get_type(reply) == DBUS_MESSAGE_TYPE_ERROR) {
    syslog(LOG_WARNING, "RegisterCallback returned error: %s",
           dbus_message_get_error_name(reply));
  }
  dbus_message_unref(reply);
}

static int floss_manager_register_callback(DBusConnection* conn) {
  const char* bt_media_object_path = CRAS_BT_OBJECT_PATH;
  DBusMessage* method_call;
  DBusPendingCall* pending_call;

  syslog(LOG_DEBUG, "Register callback to %s", BT_MANAGER_OBJECT);

  method_call =
      dbus_message_new_method_call(BT_MANAGER_SERVICE_NAME, BT_MANAGER_OBJECT,
                                   BT_MANAGER_INTERFACE, "RegisterCallback");
  if (!method_call) {
    return -ENOMEM;
  }

  if (!dbus_message_append_args(method_call, DBUS_TYPE_OBJECT_PATH,
                                &bt_media_object_path, DBUS_TYPE_INVALID)) {
    dbus_message_unref(method_call);
    return -ENOMEM;
  }

  pending_call = NULL;
  if (!dbus_connection_send_with_reply(conn, method_call, &pending_call,
                                       DBUS_TIMEOUT_USE_DEFAULT)) {
    dbus_message_unref(method_call);
    return -ENOMEM;
  }

  dbus_message_unref(method_call);
  if (!pending_call) {
    return -EIO;
  }

  if (!dbus_pending_call_set_notify(
          pending_call, floss_manager_on_register_callback, conn, NULL)) {
    dbus_pending_call_cancel(pending_call);
    dbus_pending_call_unref(pending_call);
    return -ENOMEM;
  }

  return 0;
}

static void floss_manager_on_get_adapter_enabled(DBusPendingCall* pending_call,
                                                 void* data) {
  DBusMessage* reply;
  DBusError dbus_error;
  DBusConnection* conn = (DBusConnection*)data;
  dbus_bool_t enabled = 0;

  reply = dbus_pending_call_steal_reply(pending_call);
  dbus_pending_call_unref(pending_call);

  if (dbus_message_get_type(reply) == DBUS_MESSAGE_TYPE_ERROR) {
    syslog(LOG_WARNING, "RegisterCallback returned error: %s",
           dbus_message_get_error_name(reply));
    dbus_message_unref(reply);
    return;
  }

  dbus_error_init(&dbus_error);
  if (!dbus_message_get_args(reply, &dbus_error, DBUS_TYPE_BOOLEAN, &enabled,
                             DBUS_TYPE_INVALID)) {
    syslog(LOG_WARNING, "Bad method received: %s", dbus_error.message);
    dbus_error_free(&dbus_error);
  }

  syslog(LOG_DEBUG, "GetAdapterEnabled receives reply, state %d", enabled);
  if (!enabled) {
    BTLOG(btlog, BT_ADAPTER_REMOVED, 0, 0);
    floss_media_stop(conn);
  } else {
    BTLOG(btlog, BT_ADAPTER_ADDED, 0, 0);
    floss_media_start(conn, 0);
  }

  dbus_message_unref(reply);
}

static int floss_manager_get_adapter_enabled(DBusConnection* conn, int hci) {
  DBusMessage* method_call;
  DBusPendingCall* pending_call;
  dbus_int32_t hci_interface = hci;

  method_call =
      dbus_message_new_method_call(BT_MANAGER_SERVICE_NAME, BT_MANAGER_OBJECT,
                                   BT_MANAGER_INTERFACE, "GetAdapterEnabled");
  if (!method_call) {
    return -ENOMEM;
  }

  dbus_message_append_args(method_call, DBUS_TYPE_INT32, &hci_interface,
                           DBUS_TYPE_INVALID);

  pending_call = NULL;
  if (!dbus_connection_send_with_reply(conn, method_call, &pending_call,
                                       DBUS_TIMEOUT_USE_DEFAULT)) {
    dbus_message_unref(method_call);
    return -ENOMEM;
  }

  dbus_message_unref(method_call);
  if (!pending_call) {
    return -EIO;
  }

  if (!dbus_pending_call_set_notify(
          pending_call, floss_manager_on_get_adapter_enabled, conn, NULL)) {
    dbus_pending_call_cancel(pending_call);
    dbus_pending_call_unref(pending_call);
    return -ENOMEM;
  }

  return 0;
}

static DBusHandlerResult handle_hci_device_callback(DBusConnection* conn,
                                                    DBusMessage* message,
                                                    void* arg) {
  DBusError dbus_error;
  int hci_interface = 0;
  dbus_bool_t enabled = 0;

  syslog(LOG_DEBUG, "HCI device callback message: %s %s %s",
         dbus_message_get_path(message), dbus_message_get_interface(message),
         dbus_message_get_member(message));

  if (!dbus_message_is_method_call(message, BT_CALLBACK_INTERFACE,
                                   "OnHciEnabledChanged")) {
    return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
  }

  dbus_error_init(&dbus_error);
  if (!dbus_message_get_args(message, &dbus_error, DBUS_TYPE_INT32,
                             &hci_interface, DBUS_TYPE_BOOLEAN, &enabled,
                             DBUS_TYPE_INVALID)) {
    syslog(LOG_WARNING, "Bad method received: %s", dbus_error.message);
    dbus_error_free(&dbus_error);
    return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
  }

  syslog(LOG_DEBUG, "OnHciEnabledChanged %d %d", hci_interface, enabled);
  if (enabled) {
    floss_media_start(conn, hci_interface);
  } else {
    floss_media_stop(conn);
  }

  return DBUS_HANDLER_RESULT_HANDLED;
}

// Things to do when bluetooth manager interface is added.
static void floss_on_bt_manager_added(DBusConnection* conn) {
  BTLOG(btlog, BT_MANAGER_ADDED, 0, 0);
  floss_manager_register_callback(conn);
  // TODO(b/191906229) query the adapter index to support non-default one.
  floss_manager_get_adapter_enabled(conn, 0);
}

// Things to do when bluetooth Manager interface is removed.
static void floss_on_bt_manager_removed(DBusConnection* conn) {
  BTLOG(btlog, BT_MANAGER_REMOVED, 0, 0);
}

static void floss_on_get_managed_objects(DBusPendingCall* pending_call,
                                         void* data) {
  DBusConnection* conn = (DBusConnection*)data;
  DBusMessage* reply;
  DBusMessageIter message_iter, object_array_iter;

  syslog(LOG_DEBUG, "floss_on_get_managed_objects");

  reply = dbus_pending_call_steal_reply(pending_call);
  dbus_pending_call_unref(pending_call);

  if (dbus_message_get_type(reply) == DBUS_MESSAGE_TYPE_ERROR) {
    syslog(LOG_WARNING, "GetManagedObjects returned error: %s",
           dbus_message_get_error_name(reply));
    dbus_message_unref(reply);
    return;
  }

  if (!dbus_message_has_signature(reply, "a{oa{sa{sv}}}")) {
    syslog(LOG_WARNING, "Bad GetManagedObjects reply received");
    dbus_message_unref(reply);
    return;
  }

  dbus_message_iter_init(reply, &message_iter);
  dbus_message_iter_recurse(&message_iter, &object_array_iter);

  while (dbus_message_iter_get_arg_type(&object_array_iter) !=
         DBUS_TYPE_INVALID) {
    DBusMessageIter object_dict_iter;
    const char* object_path;

    dbus_message_iter_recurse(&object_array_iter, &object_dict_iter);

    dbus_message_iter_get_basic(&object_dict_iter, &object_path);

    if (strcmp(object_path, BT_MANAGER_OBJECT) == 0) {
      floss_on_bt_manager_added(conn);
    }

    dbus_message_iter_next(&object_array_iter);
  }

  dbus_message_unref(reply);
}

static int floss_get_managed_objects(DBusConnection* conn) {
  DBusMessage* method_call;
  DBusPendingCall* pending_call;

  method_call = dbus_message_new_method_call(BT_MANAGER_SERVICE_NAME, "/",
                                             DBUS_INTERFACE_OBJECT_MANAGER,
                                             "GetManagedObjects");
  if (!method_call) {
    return -ENOMEM;
  }

  pending_call = NULL;
  if (!dbus_connection_send_with_reply(conn, method_call, &pending_call,
                                       DBUS_TIMEOUT_USE_DEFAULT)) {
    dbus_message_unref(method_call);
    return -ENOMEM;
  }

  dbus_message_unref(method_call);
  if (!pending_call) {
    return -EIO;
  }

  if (!dbus_pending_call_set_notify(pending_call, floss_on_get_managed_objects,
                                    conn, NULL)) {
    dbus_pending_call_cancel(pending_call);
    dbus_pending_call_unref(pending_call);
    return -ENOMEM;
  }

  return 0;
}

static DBusHandlerResult floss_handle_name_owner_changed(DBusConnection* conn,
                                                         DBusMessage* message,
                                                         void* arg) {
  DBusError dbus_error;
  const char *service_name, *old_owner, *new_owner;

  if (!dbus_message_is_signal(message, DBUS_INTERFACE_DBUS,
                              "NameOwnerChanged")) {
    return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
  }

  dbus_error_init(&dbus_error);
  if (!dbus_message_get_args(message, &dbus_error, DBUS_TYPE_STRING,
                             &service_name, DBUS_TYPE_STRING, &old_owner,
                             DBUS_TYPE_STRING, &new_owner, DBUS_TYPE_INVALID)) {
    syslog(LOG_WARNING, "Bad NameOwnerChanged signal received: %s",
           dbus_error.message);
    dbus_error_free(&dbus_error);
    return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
  }

  syslog(LOG_DEBUG, "%s disconnected from the bus. old:%s, new:%s",
         service_name, old_owner, new_owner);

  if (strlen(new_owner) > 0) {
    // Anything cached should be cleaned up here.
    floss_get_managed_objects(conn);
  }

  return DBUS_HANDLER_RESULT_HANDLED;
}

static DBusHandlerResult floss_handle_interfaces_added(DBusConnection* conn,
                                                       DBusMessage* message,
                                                       void* arg) {
  DBusMessageIter message_iter;
  const char* object_path;

  if (!dbus_message_is_signal(message, DBUS_INTERFACE_OBJECT_MANAGER,
                              "InterfacesAdded")) {
    return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
  }

  if (!dbus_message_has_signature(message, "oa{sa{sv}}")) {
    syslog(LOG_WARNING, "Bad InterfacesAdded signal received");
    return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
  }

  dbus_message_iter_init(message, &message_iter);
  dbus_message_iter_get_basic(&message_iter, &object_path);

  if (strcmp(object_path, BT_MANAGER_OBJECT) == 0) {
    floss_on_bt_manager_added(conn);
  }

  return DBUS_HANDLER_RESULT_HANDLED;
}

static DBusHandlerResult floss_handle_interfaces_removed(DBusConnection* conn,
                                                         DBusMessage* message,
                                                         void* arg) {
  DBusMessageIter message_iter, interface_array_iter;
  const char* object_path;

  if (!dbus_message_is_signal(message, DBUS_INTERFACE_OBJECT_MANAGER,
                              "InterfacesRemoved")) {
    return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
  }

  if (!dbus_message_has_signature(message, "oas")) {
    syslog(LOG_WARNING, "Bad InterfacesRemoved signal received");
    return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
  }

  dbus_message_iter_init(message, &message_iter);

  dbus_message_iter_get_basic(&message_iter, &object_path);
  dbus_message_iter_next(&message_iter);

  dbus_message_iter_recurse(&message_iter, &interface_array_iter);

  while (dbus_message_iter_get_arg_type(&interface_array_iter) !=
         DBUS_TYPE_INVALID) {
    const char* interface_name;

    dbus_message_iter_get_basic(&interface_array_iter, &interface_name);

    syslog(LOG_DEBUG, "InterfacesRemoved %s %s", object_path, interface_name);

    if (strcmp(object_path, BT_MANAGER_OBJECT) == 0) {
      floss_on_bt_manager_removed(conn);
    }

    dbus_message_iter_next(&interface_array_iter);
  }

  return DBUS_HANDLER_RESULT_HANDLED;
}

static void floss_start(struct bt_stack* s) {
  static const DBusObjectPathVTable control_vtable = {
      .message_function = handle_hci_device_callback,
  };
  DBusError dbus_error;

  cras_bt_policy_start();

  dbus_error_init(&dbus_error);
  dbus_bus_add_match(s->conn,
                     "type='signal',"
                     "sender='" DBUS_SERVICE_DBUS
                     "',"
                     "interface='" DBUS_INTERFACE_DBUS
                     "',"
                     "member='NameOwnerChanged',"
                     "arg0='" BT_MANAGER_SERVICE_NAME "'",
                     &dbus_error);
  if (dbus_error_is_set(&dbus_error)) {
    goto add_match_error;
  }

  dbus_bus_add_match(s->conn,
                     "type='signal',"
                     "sender='" BT_MANAGER_SERVICE_NAME
                     "',"
                     "interface='" DBUS_INTERFACE_OBJECT_MANAGER
                     "',"
                     "member='InterfacesAdded'",
                     &dbus_error);
  if (dbus_error_is_set(&dbus_error)) {
    goto add_match_error;
  }

  dbus_bus_add_match(s->conn,
                     "type='signal',"
                     "sender='" BT_MANAGER_SERVICE_NAME
                     "',"
                     "interface='" DBUS_INTERFACE_OBJECT_MANAGER
                     "',"
                     "member='InterfacesRemoved'",
                     &dbus_error);
  if (dbus_error_is_set(&dbus_error)) {
    goto add_match_error;
  }

  if (!dbus_connection_add_filter(s->conn, floss_handle_name_owner_changed,
                                  NULL, NULL) ||
      !dbus_connection_add_filter(s->conn, floss_handle_interfaces_added, NULL,
                                  NULL) ||
      !dbus_connection_add_filter(s->conn, floss_handle_interfaces_removed,
                                  NULL, NULL)) {
    goto add_filter_error;
  }

  // Register the callbacks to dbus daemon.
  if (!dbus_connection_register_object_path(s->conn, CRAS_BT_OBJECT_PATH,
                                            &control_vtable, &dbus_error)) {
    syslog(LOG_WARNING, "Couldn't register HCI device callback: %s: %s",
           CRAS_BT_OBJECT_PATH, dbus_error.message);
    dbus_error_free(&dbus_error);
    return;
  }

  floss_get_managed_objects(s->conn);
  return;

add_match_error:
  syslog(LOG_WARNING, "dBus bus add match fails: %s", dbus_error.message);
  dbus_error_free(&dbus_error);
  s->stop(s);
  return;

add_filter_error:
  syslog(LOG_WARNING, "dBus connection add filter error: %s",
         cras_strerror(ENOMEM));
  s->stop(s);
  return;
}

static void floss_stop(struct bt_stack* s) {
  cras_bt_policy_stop();

  dbus_bus_remove_match(s->conn,
                        "type='signal',"
                        "sender='" DBUS_SERVICE_DBUS
                        "',"
                        "interface='" DBUS_INTERFACE_DBUS
                        "',"
                        "member='NameOwnerChanged',"
                        "arg0='" BT_MANAGER_SERVICE_NAME "'",
                        NULL);
  dbus_bus_remove_match(s->conn,
                        "type='signal',"
                        "sender='" BT_MANAGER_SERVICE_NAME
                        "',"
                        "interface='" DBUS_INTERFACE_OBJECT_MANAGER
                        "',"
                        "member='InterfacesAdded'",
                        NULL);
  dbus_bus_remove_match(s->conn,
                        "type='signal',"
                        "sender='" BT_MANAGER_SERVICE_NAME
                        "',"
                        "interface='" DBUS_INTERFACE_OBJECT_MANAGER
                        "',"
                        "member='InterfacesRemoved'",
                        NULL);
  dbus_connection_remove_filter(s->conn, floss_handle_name_owner_changed, NULL);
  dbus_connection_remove_filter(s->conn, floss_handle_interfaces_added, NULL);
  dbus_connection_remove_filter(s->conn, floss_handle_interfaces_removed, NULL);
  dbus_connection_unregister_object_path(s->conn, CRAS_BT_OBJECT_PATH);

  floss_media_stop(s->conn);
}

static bool floss_enabled;
static struct bt_stack floss = {
    .conn = NULL,
    .start = floss_start,
    .stop = floss_stop,
};

void cras_floss_set_enabled(bool enable) {
  floss_enabled = enable;
  if (enable) {
    cras_bt_switch_stack(&floss);
  } else {
    cras_bt_switch_default_stack();
  }
}

bool cras_floss_get_enabled() {
  return floss_enabled;
}

int cras_floss_get_a2dp_enabled() {
  return !(floss.profile_disable_mask & CRAS_BT_PROFILE_MASK_A2DP);
}

int cras_floss_get_hfp_enabled() {
  return !(floss.profile_disable_mask & CRAS_BT_PROFILE_MASK_HFP);
}
