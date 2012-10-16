/* Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <dbus/dbus.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <syslog.h>

#include "cras_dbus.h"


struct bluetooth_adapter_t {
	char *object_path;
};
static struct bluetooth_adapter_t bluetooth_adapter;


static void bluetooth_adapter_clear()
{
	if (!bluetooth_adapter.object_path)
		return;

	free(bluetooth_adapter.object_path);
	bluetooth_adapter.object_path = NULL;

	syslog(LOG_DEBUG, "Bluetooth adapter lost.");
}

static void bluetooth_adapter_set(const char *adapter_path)
{
	if (bluetooth_adapter.object_path)
		bluetooth_adapter_clear();

	bluetooth_adapter.object_path = strdup(adapter_path);

	syslog(LOG_DEBUG, "Bluetooth adapter present at %s",
	       bluetooth_adapter.object_path);
}

const char *cras_bluetooth_adapter_object_path()
{
	return bluetooth_adapter.object_path;
}


static void bluetooth_on_default_adapter(DBusPendingCall *pending_call,
					 void *data)
{
	DBusError dbus_error;
	DBusMessage *reply;
	const char *adapter_path;

	reply = dbus_pending_call_steal_reply(pending_call);

	if (dbus_message_is_error(reply, "org.bluez.Error.NoSuchAdapter")) {
		bluetooth_adapter_clear();
		return;
	} else if (dbus_message_is_error(
			reply, "org.freedesktop.DBus.Error.ServiceUnknown")) {
		bluetooth_adapter_clear();
		return;
	} else if (dbus_message_get_type(reply) == DBUS_MESSAGE_TYPE_ERROR) {
		syslog(LOG_WARNING, "DefaultAdapter returned error: %s",
		       dbus_message_get_error_name(reply));
		return;
	}

	dbus_error_init(&dbus_error);

	if (!dbus_message_get_args(reply, &dbus_error,
				   DBUS_TYPE_OBJECT_PATH, &adapter_path,
				   DBUS_TYPE_INVALID)) {
		syslog(LOG_WARNING, "Bad DefaultAdapter reply received: %s",
		       dbus_error.message);
		dbus_error_free(&dbus_error);

		bluetooth_adapter_clear();
		dbus_pending_call_unref(pending_call);
		return;
	}

	bluetooth_adapter_set(adapter_path);

	dbus_pending_call_unref(pending_call);
}

static int bluetooth_get_default_adapter(DBusConnection *conn)
{
	DBusMessage *method_call;
	DBusPendingCall *pending_call;

	method_call = dbus_message_new_method_call("org.bluez",
						   "/",
						   "org.bluez.Manager",
						   "DefaultAdapter");
	if (!method_call)
		return -ENOMEM;

	if (!dbus_connection_send_with_reply(conn, method_call,
					     &pending_call,
					     DBUS_TIMEOUT_USE_DEFAULT)) {
		dbus_message_unref(method_call);
		return -ENOMEM;
	}

	if (!dbus_pending_call_set_notify(pending_call,
					  bluetooth_on_default_adapter,
					  NULL, NULL)) {
		dbus_message_unref(method_call);
		dbus_pending_call_cancel(pending_call);
		dbus_pending_call_unref(pending_call);
		return -ENOMEM;
	}

	dbus_message_unref(method_call);

	return 0;
}


static DBusHandlerResult bluetooth_handle_name_owner_changed(
	DBusConnection *conn,
	DBusMessage *message,
	void *arg)
{
	DBusError dbus_error;
	const char *name;
	const char *old_owner;
	const char *new_owner;

	if (!dbus_message_is_signal(message, "org.freedesktop.DBus",
				    "NameOwnerChanged"))
		return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;

	dbus_error_init(&dbus_error);

	if (!dbus_message_get_args(message, &dbus_error,
				   DBUS_TYPE_STRING, &name,
				   DBUS_TYPE_STRING, &old_owner,
				   DBUS_TYPE_STRING, &new_owner,
				   DBUS_TYPE_INVALID)) {
		syslog(LOG_WARNING, "Bad NameOwnerChanged signal received: %s",
		       dbus_error.message);
		dbus_error_free(&dbus_error);
		return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
	}

	bluetooth_adapter_clear();
	if (strlen(new_owner) > 0)
		bluetooth_get_default_adapter(conn);

	return DBUS_HANDLER_RESULT_HANDLED;
}

static DBusHandlerResult bluetooth_handle_default_adapter_changed(
	DBusConnection *conn,
	DBusMessage *message,
	void *arg)
{
	DBusError dbus_error;
	const char *new_adapter_path;

	if (!dbus_message_is_signal(message, "org.bluez.Manager",
				    "DefaultAdapterChanged"))
		return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;

	dbus_error_init(&dbus_error);

	if (!dbus_message_get_args(message, &dbus_error,
				   DBUS_TYPE_OBJECT_PATH, &new_adapter_path,
				   DBUS_TYPE_INVALID)) {
		syslog(LOG_WARNING,
		       "Bad DefaultAdapterChanged signal received: %s",
		       dbus_error.message);
		dbus_error_free(&dbus_error);
		return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
	}

	bluetooth_adapter_set(new_adapter_path);

	return DBUS_HANDLER_RESULT_HANDLED;
}

static DBusHandlerResult bluetooth_handle_adapter_removed(
	DBusConnection *conn,
	DBusMessage *message,
	void *arg)
{
	DBusError dbus_error;
	const char *adapter_path;

	if (!dbus_message_is_signal(message, "org.bluez.Manager",
				    "AdapterRemoved"))
		return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;

	dbus_error_init(&dbus_error);

	if (!dbus_message_get_args(message, &dbus_error,
				   DBUS_TYPE_OBJECT_PATH, &adapter_path,
				   DBUS_TYPE_INVALID)) {
		syslog(LOG_WARNING, "Bad AdapterRemoved signal received: %s",
		       dbus_error.message);
		dbus_error_free(&dbus_error);
		return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
	}

	if (bluetooth_adapter.object_path &&
	    strcmp(bluetooth_adapter.object_path, adapter_path) == 0)
		bluetooth_adapter_clear();

	return DBUS_HANDLER_RESULT_HANDLED;
}


void cras_bluetooth_start(DBusConnection *conn)
{
	DBusError dbus_error;

	dbus_error_init(&dbus_error);

	dbus_bus_add_match(conn,
			   "type='signal',"
			   "sender='org.freedesktop.DBus',"
			   "interface='org.freedesktop.DBus',"
			   "member='NameOwnerChanged',"
			   "arg0='org.bluez'",
			   &dbus_error);
	if (dbus_error_is_set(&dbus_error))
		goto add_match_error;
	if (!dbus_connection_add_filter(
		conn,
		bluetooth_handle_name_owner_changed,
		NULL, NULL))
		goto add_filter_error;

	dbus_bus_add_match(conn,
			   "type='signal',"
			   "sender='org.bluez',"
			   "interface='org.bluez.Manager',"
			   "member='DefaultAdapterChanged'",
			   &dbus_error);
	if (dbus_error_is_set(&dbus_error))
		goto add_match_error;
	if (!dbus_connection_add_filter(
		conn,
		bluetooth_handle_default_adapter_changed,
		NULL, NULL))
		goto add_filter_error;

	dbus_bus_add_match(conn,
			   "type='signal',"
			   "sender='org.bluez',"
			   "interface='org.bluez.Manager',"
			   "member='AdapterRemoved'",
			   &dbus_error);
	if (dbus_error_is_set(&dbus_error))
		goto add_match_error;
	if (!dbus_connection_add_filter(
		conn,
		bluetooth_handle_adapter_removed,
		NULL, NULL))
		goto add_filter_error;

	bluetooth_get_default_adapter(conn);

	return;

add_match_error:
	syslog(LOG_WARNING,
	       "Couldn't setup monitoring for Bluetooth devices: %s",
	       dbus_error.message);
	dbus_error_free(&dbus_error);
	return;
add_filter_error:
	syslog(LOG_WARNING,
	       "Couldn't setup monitoring for Bluetooth devices: %s",
	       strerror(ENOMEM));
	return;
}

void cras_bluetooth_stop(DBusConnection *conn)
{
	dbus_bus_remove_match(conn,
			      "type='signal',"
			      "sender='org.freedesktop.DBus',"
			      "interface='org.freedesktop.DBus',"
			      "member='NameOwnerChanged',"
			      "arg0='org.bluez'",
			      NULL);
	dbus_connection_remove_filter(
	    conn,
	    bluetooth_handle_name_owner_changed,
	    NULL);

	dbus_bus_remove_match(conn,
			      "type='signal',"
			      "sender='org.bluez',"
			      "interface='org.bluez.Manager',"
			      "member='DefaultAdapterChanged'",
			      NULL);
	dbus_connection_remove_filter(
	    conn,
	    bluetooth_handle_default_adapter_changed,
	    NULL);

	dbus_bus_remove_match(conn,
			      "type='signal',"
			      "sender='org.bluez',"
			      "interface='org.bluez.Manager',"
			      "member='AdapterRemoved'",
			      NULL);
	dbus_connection_remove_filter(
	    conn,
	    bluetooth_handle_adapter_removed,
	    NULL);

	bluetooth_adapter_clear();
}
