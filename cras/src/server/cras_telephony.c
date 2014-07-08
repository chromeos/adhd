/* Copyright (c) 2014 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <string.h>
#include <stdlib.h>
#include <syslog.h>

#include <dbus/dbus.h>

#include "cras_telephony.h"
#include "cras_hfp_slc.h"

#define CRAS_TELEPHONY_INTERFACE "org.chromium.cras.Telephony"
#define CRAS_TELEPHONY_OBJECT_PATH "/org/chromium/cras/telephony"
#define TELEPHONY_INTROSPECT_XML					\
	DBUS_INTROSPECT_1_0_XML_DOCTYPE_DECL_NODE			\
	"<node>\n"							\
	"  <interface name=\"" CRAS_TELEPHONY_INTERFACE "\">\n"		\
	"    <method name=\"AnswerCall\">\n"				\
	"    </method>\n"						\
	"    <method name=\"IncomingCall\">\n"				\
	"    </method>\n"						\
	"    <method name=\"TerminateCall\">\n"				\
	"    </method>\n"						\
	"    <method name=\"SetBatteryLevel\">\n"				\
	"      <arg name=\"value\" type=\"i\" direction=\"in\"/>\n"	\
	"    </method>\n"						\
	"    <method name=\"SetSignalStrength\">\n"				\
	"      <arg name=\"value\" type=\"i\" direction=\"in\"/>\n"	\
	"    </method>\n"						\
	"    <method name=\"SetServiceAvailability\">\n"				\
	"      <arg name=\"value\" type=\"i\" direction=\"in\"/>\n"	\
	"    </method>\n"						\
	"    <method name=\"StoreDialNumber\">\n"			\
	"    </method>\n"						\
	"  </interface>\n"						\
	"  <interface name=\"" DBUS_INTERFACE_INTROSPECTABLE "\">\n"	\
	"    <method name=\"Introspect\">\n"				\
	"      <arg name=\"data\" type=\"s\" direction=\"out\"/>\n"	\
	"    </method>\n"						\
	"  </interface>\n"						\
	"</node>\n"

static struct cras_telephony_handle telephony_handle;

/* Helper to send an empty reply. */
static void send_empty_reply(DBusConnection *conn, DBusMessage *message)
{
	DBusMessage *reply;
	dbus_uint32_t serial = 0;

	reply = dbus_message_new_method_return(message);
	if (!reply)
		return;

	dbus_connection_send(conn, reply, &serial);

	dbus_message_unref(reply);
}

static DBusHandlerResult handle_telephony_message(DBusConnection *conn,
						  DBusMessage *message,
						  void *arg)
{
	struct hfp_slc_handle *handle ;

	syslog(LOG_ERR, "Telephony message: %s %s %s",
			dbus_message_get_path(message),
			dbus_message_get_interface(message),
			dbus_message_get_member(message));

	if (dbus_message_is_method_call(message,
					DBUS_INTERFACE_INTROSPECTABLE,
					"Introspect")) {
		DBusMessage *reply;
		const char *xml = TELEPHONY_INTROSPECT_XML;

		reply = dbus_message_new_method_return(message);
		if (!reply)
			return DBUS_HANDLER_RESULT_NEED_MEMORY;
		if (!dbus_message_append_args(reply,
					      DBUS_TYPE_STRING, &xml,
					      DBUS_TYPE_INVALID))
			return DBUS_HANDLER_RESULT_NEED_MEMORY;
		if (!dbus_connection_send(conn, reply, NULL))
			return DBUS_HANDLER_RESULT_NEED_MEMORY;

		dbus_message_unref(reply);
		return DBUS_HANDLER_RESULT_HANDLED;
	} else if (dbus_message_is_method_call(message,
					       CRAS_TELEPHONY_INTERFACE,
					       "IncomingCall")) {

		handle = hfp_slc_get_handle();
		if (handle)
			/* Fake number with type NUMBER_TYPE_TELEPHONY */
			hfp_event_incoming_call(handle, "1234567", 129);

		// TODO(menghuan): provide APIs to handle
		telephony_handle.callsetup = 1;

		send_empty_reply(conn, message);
		return DBUS_HANDLER_RESULT_HANDLED;
	} else if (dbus_message_is_method_call(message,
					       CRAS_TELEPHONY_INTERFACE,
					       "TerminateCall")) {

		handle = hfp_slc_get_handle();
		if (handle)
			hfp_event_terminate_call(handle);

		// TODO(menghuan): provide APIs to handle
		telephony_handle.call = 1;
		telephony_handle.callsetup = 0;

		send_empty_reply(conn, message);
		return DBUS_HANDLER_RESULT_HANDLED;
	} else if (dbus_message_is_method_call(message,
					       CRAS_TELEPHONY_INTERFACE,
					       "AnswerCall")) {

		handle = hfp_slc_get_handle();
		if (handle)
			hfp_event_answer_call(handle);

		// TODO(menghuan): provide APIs to handle
		telephony_handle.call = 0;
		telephony_handle.callsetup = 0;

		send_empty_reply(conn, message);
		return DBUS_HANDLER_RESULT_HANDLED;
	} else if (dbus_message_is_method_call(message,
					       CRAS_TELEPHONY_INTERFACE,
					       "StoreDialNumber")) {

		handle = hfp_slc_get_handle();
		if (handle)
			hfp_event_store_dial_number(handle, "1234567");

		send_empty_reply(conn, message);
		return DBUS_HANDLER_RESULT_HANDLED;
	} else if (dbus_message_is_method_call(message,
					       CRAS_TELEPHONY_INTERFACE,
					       "SetBatteryLevel")) {
		dbus_int32_t level;
		DBusError dbus_error;

		dbus_error_init(&dbus_error);

		if (!dbus_message_get_args(message, &dbus_error,
					   DBUS_TYPE_INT32, &level,
					   DBUS_TYPE_INVALID)) {
			syslog(LOG_WARNING,
			       "Bad method received: %s",
			       dbus_error.message);
			dbus_error_free(&dbus_error);
			return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
		}
		handle = hfp_slc_get_handle();
		if (handle)
			hfp_event_set_battery(handle, level);

		send_empty_reply(conn, message);
		return DBUS_HANDLER_RESULT_HANDLED;
	} else if (dbus_message_is_method_call(message,
					       CRAS_TELEPHONY_INTERFACE,
					       "SetSignalStrength")) {
		dbus_int32_t level;
		DBusError dbus_error;

		dbus_error_init(&dbus_error);

		if (!dbus_message_get_args(message, &dbus_error,
					   DBUS_TYPE_INT32, &level,
					   DBUS_TYPE_INVALID)) {
			syslog(LOG_WARNING,
			       "Bad method received: %s",
			       dbus_error.message);
			dbus_error_free(&dbus_error);
			return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
		}
		handle = hfp_slc_get_handle();
		if (handle)
			hfp_event_set_signal(handle, level);

		send_empty_reply(conn, message);
		return DBUS_HANDLER_RESULT_HANDLED;
	} else if (dbus_message_is_method_call(message,
					       CRAS_TELEPHONY_INTERFACE,
					       "SetServiceAvailability")) {
		dbus_int32_t level;
		DBusError dbus_error;

		dbus_error_init(&dbus_error);

		if (!dbus_message_get_args(message, &dbus_error,
					   DBUS_TYPE_INT32, &level,
					   DBUS_TYPE_INVALID)) {
			syslog(LOG_WARNING,
			       "Bad method received: %s",
			       dbus_error.message);
			dbus_error_free(&dbus_error);
			return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
		}
		handle = hfp_slc_get_handle();
		if (handle)
			hfp_event_set_service(handle, level);

		send_empty_reply(conn, message);
		return DBUS_HANDLER_RESULT_HANDLED;
	}

	return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
}

/* Exported Interface */

void cras_telephony_start(DBusConnection *conn)
{
	static const DBusObjectPathVTable control_vtable = {
		.message_function = handle_telephony_message,
	};

	DBusError dbus_error;

	telephony_handle.dbus_conn = conn;
	dbus_connection_ref(telephony_handle.dbus_conn);

	if (!dbus_connection_register_object_path(conn,
						  CRAS_TELEPHONY_OBJECT_PATH,
						  &control_vtable,
						  &dbus_error)) {
		syslog(LOG_ERR,
		       "Couldn't register telephony control: %s: %s",
		       CRAS_TELEPHONY_OBJECT_PATH, dbus_error.message);
		dbus_error_free(&dbus_error);
		return;
	}
}

void cras_telephony_stop()
{
	if (!telephony_handle.dbus_conn)
		return;

	dbus_connection_unregister_object_path(telephony_handle.dbus_conn,
					       CRAS_TELEPHONY_OBJECT_PATH);
	dbus_connection_unref(telephony_handle.dbus_conn);
	telephony_handle.dbus_conn = NULL;
}

struct cras_telephony_handle* cras_telephony_get()
{
	return &telephony_handle;
}
