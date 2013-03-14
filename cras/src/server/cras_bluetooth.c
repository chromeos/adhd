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
#include "cras_bluetooth.h"
#include "utlist.h"

struct cras_bluetooth_device {
	char *object_path;
	struct cras_bluetooth_device *prev, *next;
};

struct cras_bluetooth_adapter {
	DBusConnection *conn;
	char *object_path;
	DBusPendingCall *pending_call;
	struct cras_bluetooth_device *devices;
};
static struct cras_bluetooth_adapter default_adapter;

static void device_free(struct cras_bluetooth_device *device)
{
	free(device->object_path);
	free(device);
}

static struct cras_bluetooth_device *default_adapter_add_device(
	const char *device_path)
{
	struct cras_bluetooth_device *device;

	for (device = default_adapter.devices; device; device = device->next)
		if (strcmp(device->object_path, device_path) == 0)
			return device;

	device = calloc(1, sizeof(*device));
	if (device == NULL)
		return NULL;
	device->object_path = strdup(device_path);
	if (device->object_path == NULL) {
		device_free(device);
		return NULL;
	}
	LL_PREPEND(default_adapter.devices, device);

	syslog(LOG_DEBUG, "Bluetooth device added at %s",
	       device->object_path);
	return device;
}

static void default_adapter_remove_device(const char *device_path)
{
	struct cras_bluetooth_device **device;

	for (device = &default_adapter.devices; *device;
	     device = &((*device)->next)) {
		if (strcmp((*device)->object_path, device_path) == 0) {
			struct cras_bluetooth_device *tmp = *device;
			*device = (*device)->next;

			syslog(LOG_DEBUG, "Bluetooth device removed from %s",
			       tmp->object_path);
			device_free(tmp);
			break;
		}
	}
}

static void default_adapter_set_devices(DBusMessageIter *iter)
{
	DBusMessageIter array_iter;
	char *signature;
	int match;

	/* Check if the signature is "ao" */
	signature = dbus_message_iter_get_signature(iter);
	if (!signature)
		return;
	match = (strcmp("ao", signature) == 0);
	dbus_free(signature);
	if (!match)
		return;

	dbus_message_iter_recurse(iter, &array_iter);
	while (dbus_message_iter_get_arg_type(&array_iter) !=
	       DBUS_TYPE_INVALID) {
		const char *device_path;

		dbus_message_iter_get_basic(&array_iter, &device_path);
		default_adapter_add_device(device_path);

		dbus_message_iter_next(&array_iter);
	}
}

static void default_adapter_on_get_properties(DBusPendingCall *pending_call,
					      void *data)
{
	DBusMessage *reply;
	DBusMessageIter message_iter;
	DBusMessageIter array_iter;

	reply = dbus_pending_call_steal_reply(pending_call);
	dbus_pending_call_unref(pending_call);
	default_adapter.pending_call = NULL;

	if (dbus_message_is_error(reply, "org.bluez.Error.NoSuchAdapter")) {
		dbus_message_unref(reply);
		return;
	} else if (dbus_message_is_error(reply, "org.bluez.Error.NotReady")) {
		dbus_message_unref(reply);
		return;
	} else if (dbus_message_is_error(
			reply, "org.freedesktop.DBus.Error.ServiceUnknown")) {
		dbus_message_unref(reply);
		return;
	} else if (dbus_message_get_type(reply) == DBUS_MESSAGE_TYPE_ERROR) {
		syslog(LOG_WARNING, "Adapter.GetProperties returned error: %s",
		       dbus_message_get_error_name(reply));
		dbus_message_unref(reply);
		return;
	}

	if (!dbus_message_has_signature(reply, "a{sv}")) {
		syslog(LOG_WARNING, "Bad Adapter.GetProperties reply received");
		dbus_message_unref(reply);
		return;
	}

	dbus_message_iter_init(reply, &message_iter);
	dbus_message_iter_recurse(&message_iter, &array_iter);

	while (dbus_message_iter_get_arg_type(&array_iter) !=
	       DBUS_TYPE_INVALID) {
		DBusMessageIter struct_iter;
		DBusMessageIter variant_iter;
		const char *key;

		dbus_message_iter_recurse(&array_iter, &struct_iter);

		dbus_message_iter_get_basic(&struct_iter, &key);
		dbus_message_iter_next(&struct_iter);
		dbus_message_iter_recurse(&struct_iter, &variant_iter);

		if (strcmp("Devices", key) == 0)
			default_adapter_set_devices(&variant_iter);

		dbus_message_iter_next(&array_iter);
	}

	dbus_message_unref(reply);
}

static int default_adapter_get_properties()
{
	DBusMessage *method_call;
	DBusPendingCall *pending_call;

	method_call = dbus_message_new_method_call("org.bluez",
						   default_adapter.object_path,
						   "org.bluez.Adapter",
						   "GetProperties");
	if (!method_call)
		return -ENOMEM;

	pending_call = NULL;
	if (!dbus_connection_send_with_reply(default_adapter.conn, method_call,
					     &pending_call,
					     DBUS_TIMEOUT_USE_DEFAULT)) {
		dbus_message_unref(method_call);
		return -ENOMEM;
	}

	dbus_message_unref(method_call);
	if (!pending_call)
		return 0;

	if (!dbus_pending_call_set_notify(pending_call,
					  default_adapter_on_get_properties,
					  NULL, NULL)) {
		dbus_pending_call_cancel(pending_call);
		dbus_pending_call_unref(pending_call);
		return -ENOMEM;
	}

	default_adapter.pending_call = pending_call;

	return 0;
}


static void default_adapter_clear()
{
	if (!default_adapter.object_path)
		return;

	free(default_adapter.object_path);
	default_adapter.object_path = NULL;

	if (default_adapter.pending_call) {
		dbus_pending_call_cancel(default_adapter.pending_call);
		dbus_pending_call_unref(default_adapter.pending_call);
		default_adapter.pending_call = NULL;
	}

	while (default_adapter.devices) {
		struct cras_bluetooth_device *device = default_adapter.devices;
		default_adapter.devices = device->next;
		device_free(device);
	}

	syslog(LOG_DEBUG, "Bluetooth adapter lost.");
}

static void default_adapter_set(const char *adapter_path)
{
	if (default_adapter.object_path)
		default_adapter_clear();

	default_adapter.object_path = strdup(adapter_path);

	syslog(LOG_DEBUG, "Bluetooth adapter present at %s",
	       default_adapter.object_path);

	default_adapter_get_properties();
}

const char *cras_bluetooth_adapter_object_path()
{
	return default_adapter.object_path;
}

const struct cras_bluetooth_device *cras_bluetooth_adapter_first_device()
{
	return default_adapter.devices;
}

const struct cras_bluetooth_device *cras_bluetooth_adapter_next_device(
	const struct cras_bluetooth_device *device)
{
	return device->next;
}

const char *cras_bluetooth_device_object_path(
	const struct cras_bluetooth_device *device)
{
	return device->object_path;
}



static void bluetooth_on_default_adapter(DBusPendingCall *pending_call,
					 void *data)
{
	DBusError dbus_error;
	DBusMessage *reply;
	const char *adapter_path;

	reply = dbus_pending_call_steal_reply(pending_call);
	dbus_pending_call_unref(pending_call);

	if (dbus_message_is_error(reply, "org.bluez.Error.NoSuchAdapter")) {
		default_adapter_clear();
		dbus_message_unref(reply);
		return;
	} else if (dbus_message_is_error(
			reply, "org.freedesktop.DBus.Error.ServiceUnknown")) {
		default_adapter_clear();
		dbus_message_unref(reply);
		return;
	} else if (dbus_message_get_type(reply) == DBUS_MESSAGE_TYPE_ERROR) {
		syslog(LOG_WARNING, "DefaultAdapter returned error: %s",
		       dbus_message_get_error_name(reply));
		dbus_message_unref(reply);
		return;
	}

	dbus_error_init(&dbus_error);

	if (!dbus_message_get_args(reply, &dbus_error,
				   DBUS_TYPE_OBJECT_PATH, &adapter_path,
				   DBUS_TYPE_INVALID)) {
		syslog(LOG_WARNING, "Bad DefaultAdapter reply received: %s",
		       dbus_error.message);
		dbus_error_free(&dbus_error);

		default_adapter_clear();
		dbus_message_unref(reply);
		return;
	}

	default_adapter_set(adapter_path);

	dbus_message_unref(reply);
}

static int bluetooth_get_default_adapter()
{
	DBusMessage *method_call;
	DBusPendingCall *pending_call;

	method_call = dbus_message_new_method_call("org.bluez",
						   "/",
						   "org.bluez.Manager",
						   "DefaultAdapter");
	if (!method_call)
		return -ENOMEM;

	pending_call = NULL;
	if (!dbus_connection_send_with_reply(default_adapter.conn, method_call,
					     &pending_call,
					     DBUS_TIMEOUT_USE_DEFAULT)) {
		dbus_message_unref(method_call);
		return -ENOMEM;
	}

	dbus_message_unref(method_call);
	if (!pending_call)
		return 0;

	if (!dbus_pending_call_set_notify(pending_call,
					  bluetooth_on_default_adapter,
					  NULL, NULL)) {
		dbus_pending_call_cancel(pending_call);
		dbus_pending_call_unref(pending_call);
		return -ENOMEM;
	}

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

	default_adapter_clear();
	if (strlen(new_owner) > 0)
		bluetooth_get_default_adapter();

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

	default_adapter_set(new_adapter_path);

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

	if (default_adapter.object_path &&
	    strcmp(default_adapter.object_path, adapter_path) == 0)
		default_adapter_clear();

	return DBUS_HANDLER_RESULT_HANDLED;
}


static DBusHandlerResult bluetooth_handle_device_created(
	DBusConnection *conn,
	DBusMessage *message,
	void *arg)
{
	DBusError dbus_error;
	const char *device_path;

	if (!dbus_message_is_signal(message, "org.bluez.Adapter",
				    "DeviceCreated"))
		return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;

	if (!default_adapter.object_path ||
	    strcmp(dbus_message_get_path(message),
		   default_adapter.object_path) != 0)
		return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;

	dbus_error_init(&dbus_error);

	if (!dbus_message_get_args(message, &dbus_error,
				   DBUS_TYPE_OBJECT_PATH, &device_path,
				   DBUS_TYPE_INVALID)) {
		syslog(LOG_WARNING, "Bad DeviceCreated signal received: %s",
		       dbus_error.message);
		dbus_error_free(&dbus_error);
		return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
	}

	default_adapter_add_device(device_path);

	return DBUS_HANDLER_RESULT_HANDLED;
}

static DBusHandlerResult bluetooth_handle_device_removed(
	DBusConnection *conn,
	DBusMessage *message,
	void *arg)
{
	DBusError dbus_error;
	const char *device_path;

	if (!dbus_message_is_signal(message, "org.bluez.Adapter",
				    "DeviceRemoved"))
		return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;

	if (!default_adapter.object_path ||
	    strcmp(dbus_message_get_path(message),
		   default_adapter.object_path) != 0)
		return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;

	dbus_error_init(&dbus_error);

	if (!dbus_message_get_args(message, &dbus_error,
				   DBUS_TYPE_OBJECT_PATH, &device_path,
				   DBUS_TYPE_INVALID)) {
		syslog(LOG_WARNING, "Bad DeviceRemoved signal received: %s",
		       dbus_error.message);
		dbus_error_free(&dbus_error);
		return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
	}

	default_adapter_remove_device(device_path);

	return DBUS_HANDLER_RESULT_HANDLED;
}


void cras_bluetooth_start(DBusConnection *conn)
{
	DBusError dbus_error;

	default_adapter.conn = conn;
	dbus_connection_ref(default_adapter.conn);

	default_adapter.object_path = NULL;
	default_adapter.devices = NULL;

	dbus_error_init(&dbus_error);

	dbus_bus_add_match(default_adapter.conn,
			   "type='signal',"
			   "sender='org.freedesktop.DBus',"
			   "interface='org.freedesktop.DBus',"
			   "member='NameOwnerChanged',"
			   "arg0='org.bluez'",
			   &dbus_error);
	if (dbus_error_is_set(&dbus_error))
		goto add_match_error;
	if (!dbus_connection_add_filter(
			default_adapter.conn,
			bluetooth_handle_name_owner_changed,
			NULL, NULL))
		goto add_filter_error;

	dbus_bus_add_match(default_adapter.conn,
			   "type='signal',"
			   "sender='org.bluez',"
			   "interface='org.bluez.Manager',"
			   "member='DefaultAdapterChanged'",
			   &dbus_error);
	if (dbus_error_is_set(&dbus_error))
		goto add_match_error;
	if (!dbus_connection_add_filter(
			default_adapter.conn,
			bluetooth_handle_default_adapter_changed,
			NULL, NULL))
		goto add_filter_error;

	dbus_bus_add_match(default_adapter.conn,
			   "type='signal',"
			   "sender='org.bluez',"
			   "interface='org.bluez.Manager',"
			   "member='AdapterRemoved'",
			   &dbus_error);
	if (dbus_error_is_set(&dbus_error))
		goto add_match_error;
	if (!dbus_connection_add_filter(
			default_adapter.conn,
			bluetooth_handle_adapter_removed,
			NULL, NULL))
		goto add_filter_error;

	/* TODO(keybuk) adapter properties changed? powered etc. */

	dbus_bus_add_match(default_adapter.conn,
			   "type='signal',"
			   "sender='org.bluez',"
			   "interface='org.bluez.Adapter',"
			   "member='DeviceCreated'",
			   &dbus_error);
	if (dbus_error_is_set(&dbus_error))
		goto add_match_error;
	if (!dbus_connection_add_filter(
			default_adapter.conn,
			bluetooth_handle_device_created,
			NULL, NULL))
		goto add_filter_error;

	dbus_bus_add_match(default_adapter.conn,
			   "type='signal',"
			   "sender='org.bluez',"
			   "interface='org.bluez.Adapter',"
			   "member='DeviceRemoved'",
			   &dbus_error);
	if (dbus_error_is_set(&dbus_error))
		goto add_match_error;
	if (!dbus_connection_add_filter(
			default_adapter.conn,
			bluetooth_handle_device_removed,
			NULL, NULL))
		goto add_filter_error;

	/* TODO(keybuk) device property changed. */

	bluetooth_get_default_adapter();

	return;

add_match_error:
	syslog(LOG_WARNING,
	       "Couldn't setup monitoring for Bluetooth devices: %s",
	       dbus_error.message);
	dbus_error_free(&dbus_error);
	dbus_connection_unref(default_adapter.conn);
	default_adapter.conn = NULL;
	return;
add_filter_error:
	syslog(LOG_WARNING,
	       "Couldn't setup monitoring for Bluetooth devices: %s",
	       strerror(ENOMEM));
	dbus_connection_unref(default_adapter.conn);
	default_adapter.conn = NULL;
	return;
}

void cras_bluetooth_stop()
{
	if (!default_adapter.conn)
		return;

	dbus_bus_remove_match(default_adapter.conn,
			      "type='signal',"
			      "sender='org.freedesktop.DBus',"
			      "interface='org.freedesktop.DBus',"
			      "member='NameOwnerChanged',"
			      "arg0='org.bluez'",
			      NULL);
	dbus_connection_remove_filter(
	    default_adapter.conn,
	    bluetooth_handle_name_owner_changed,
	    NULL);

	dbus_bus_remove_match(default_adapter.conn,
			      "type='signal',"
			      "sender='org.bluez',"
			      "interface='org.bluez.Manager',"
			      "member='DefaultAdapterChanged'",
			      NULL);
	dbus_connection_remove_filter(
	    default_adapter.conn,
	    bluetooth_handle_default_adapter_changed,
	    NULL);

	dbus_bus_remove_match(default_adapter.conn,
			      "type='signal',"
			      "sender='org.bluez',"
			      "interface='org.bluez.Manager',"
			      "member='AdapterRemoved'",
			      NULL);
	dbus_connection_remove_filter(
	    default_adapter.conn,
	    bluetooth_handle_adapter_removed,
	    NULL);

	dbus_bus_remove_match(default_adapter.conn,
			      "type='signal',"
			      "sender='org.bluez',"
			      "interface='org.bluez.Adapter',"
			      "member='DeviceCreated'",
			      NULL);
	dbus_connection_remove_filter(
	    default_adapter.conn,
	    bluetooth_handle_device_created,
	    NULL);

	dbus_bus_remove_match(default_adapter.conn,
			      "type='signal',"
			      "sender='org.bluez',"
			      "interface='org.bluez.Adapter',"
			      "member='DeviceRemoved'",
			      NULL);
	dbus_connection_remove_filter(
	    default_adapter.conn,
	    bluetooth_handle_device_removed,
	    NULL);

	default_adapter_clear();

	dbus_connection_unref(default_adapter.conn);
	default_adapter.conn = NULL;
}
