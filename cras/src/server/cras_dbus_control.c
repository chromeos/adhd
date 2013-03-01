/* Copyright (c) 2013 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <dbus/dbus.h>
#include <errno.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <syslog.h>

#include "cras_dbus.h"
#include "cras_dbus_control.h"
#include "cras_system_state.h"
#include "cras_util.h"
#include "utlist.h"

struct cras_dbus_control {
	DBusConnection *conn;
};
static struct cras_dbus_control dbus_control;

/* helper to extract a single argument from a DBus message. */
static int get_single_arg(DBusMessage *message, int dbus_type, void *arg)
{
	DBusError dbus_error;

	dbus_error_init(&dbus_error);

	if (!dbus_message_get_args(message, &dbus_error,
				   dbus_type, arg,
				   DBUS_TYPE_INVALID)) {
		syslog(LOG_WARNING,
		       "Bad method received: %s",
		       dbus_error.message);
		dbus_error_free(&dbus_error);
		return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
	}

	return 0;
}

/* Helper to send an empty reply. */
static void send_empty_reply(DBusMessage *message)
{
	DBusMessage *reply;
	dbus_uint32_t serial = 0;

	reply = dbus_message_new_method_return(message);
	if (!reply)
		return;

	dbus_connection_send(dbus_control.conn, reply, &serial);

	dbus_message_unref(reply);
}

/* Handlers for exported DBus method calls. */
static DBusHandlerResult handle_set_system_volume(
	DBusConnection *conn,
	DBusMessage *message,
	void *arg)
{
	int rc;
	uint8_t new_vol;

	if (!dbus_message_is_method_call(message, "org.chromium.cras",
					 "SetSystemVolume"))
		return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;

	rc = get_single_arg(message, DBUS_TYPE_BYTE, &new_vol);
	if (rc)
		return rc;

	cras_system_set_volume(new_vol);

	send_empty_reply(message);

	return DBUS_HANDLER_RESULT_HANDLED;
}

static DBusHandlerResult handle_set_system_mute(
	DBusConnection *conn,
	DBusMessage *message,
	void *arg)
{
	int rc;
	dbus_bool_t new_mute;

	if (!dbus_message_is_method_call(message, "org.chromium.cras",
					 "SetSystemMute"))
		return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;

	rc = get_single_arg(message, DBUS_TYPE_BOOLEAN, &new_mute);
	if (rc)
		return rc;

	cras_system_set_mute(new_mute);

	send_empty_reply(message);

	return DBUS_HANDLER_RESULT_HANDLED;
}

static DBusHandlerResult handle_set_system_capture_gain(
	DBusConnection *conn,
	DBusMessage *message,
	void *arg)
{
	int rc;
	int32_t new_gain;

	if (!dbus_message_is_method_call(message, "org.chromium.cras",
					 "SetSystemCaptureGain"))
		return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;

	rc = get_single_arg(message, DBUS_TYPE_INT32, &new_gain);
	if (rc)
		return rc;

	cras_system_set_capture_gain(new_gain);

	send_empty_reply(message);

	return DBUS_HANDLER_RESULT_HANDLED;
}

static DBusHandlerResult handle_set_system_capture_mute(
	DBusConnection *conn,
	DBusMessage *message,
	void *arg)
{
	int rc;
	dbus_bool_t new_mute;

	if (!dbus_message_is_method_call(message, "org.chromium.cras",
					 "SetSystemCaptureMute"))
		return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;

	rc = get_single_arg(message, DBUS_TYPE_BOOLEAN, &new_mute);
	if (rc)
		return rc;

	cras_system_set_capture_mute(new_mute);

	send_empty_reply(message);

	return DBUS_HANDLER_RESULT_HANDLED;
}

static DBusHandlerResult handle_get_system_volume_state(
	DBusConnection *conn,
	DBusMessage *message,
	void *arg)
{
	DBusMessage *reply;
	dbus_uint32_t serial = 0;
	uint8_t volume;
	dbus_bool_t muted;
	dbus_int32_t capture_gain;
	dbus_bool_t capture_muted;

	if (!dbus_message_is_method_call(message, "org.chromium.cras",
					 "GetSystemVolumeState"))
		return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;

	reply = dbus_message_new_method_return(message);

	volume = cras_system_get_volume();
	muted = cras_system_get_mute();
	capture_gain = cras_system_get_capture_gain();
	capture_muted = cras_system_get_capture_mute();

	dbus_message_append_args(reply,
				 DBUS_TYPE_BYTE, &volume,
				 DBUS_TYPE_BOOLEAN, &muted,
				 DBUS_TYPE_INT32, &capture_gain,
				 DBUS_TYPE_BOOLEAN, &capture_muted,
				 DBUS_TYPE_INVALID);

	dbus_connection_send(dbus_control.conn, reply, &serial);

	dbus_message_unref(reply);

	return DBUS_HANDLER_RESULT_HANDLED;
}

/* Adds a dbus match. */
static void add_dbus_match(const char *match,
			   DBusHandlerResult (*handler)(DBusConnection *,
							DBusMessage *,
							void *))
{
	DBusError dbus_error;

	dbus_error_init(&dbus_error);

	dbus_bus_add_match(dbus_control.conn, match, &dbus_error);
	if (dbus_error_is_set(&dbus_error))
		goto add_match_error;
	if (!dbus_connection_add_filter(dbus_control.conn,
					handler,
					NULL, NULL))
		goto add_filter_error;

	return;

add_match_error:
	syslog(LOG_WARNING,
	       "Couldn't setup monitoring for control messages: %s",
	       dbus_error.message);
	dbus_error_free(&dbus_error);
	dbus_connection_unref(dbus_control.conn);
	dbus_control.conn = NULL;
	return;

add_filter_error:
	syslog(LOG_WARNING,
	       "Couldn't setup monitoring for control messages: %s",
	       strerror(ENOMEM));
	dbus_connection_unref(dbus_control.conn);
	dbus_control.conn = NULL;
}

/* Removes a match added with add_dbus_match. */
static void remove_dbus_match(const char *match,
			      DBusHandlerResult (*handler)(DBusConnection *,
							   DBusMessage *,
							   void *))
{
	dbus_bus_remove_match(dbus_control.conn, match, NULL);
	dbus_connection_remove_filter(dbus_control.conn, handler, NULL);
}

/* Creates a new DBus message, must be freed with dbus_message_unref. */
static DBusMessage *create_dbus_message(const char *name)
{
	DBusMessage *msg;
	msg = dbus_message_new_signal("/org/chromium/cras",
				       "org.chromium.cras",
				       name);
	if (!msg)
		syslog(LOG_ERR, "Failed to create signal");

	return msg;
}

/* Handlers for system updates that generate DBus signals. */

static void signal_volume(void *arg)
{
	dbus_uint32_t serial = 0;
	DBusMessage *msg;
	uint8_t volume;

	msg = create_dbus_message("SystemVolumeChanged");
	if (!msg)
		return;

	volume = cras_system_get_volume();
	dbus_message_append_args(msg,
				 DBUS_TYPE_BYTE, &volume,
				 DBUS_TYPE_INVALID);
	dbus_connection_send(dbus_control.conn, msg, &serial);
	dbus_message_unref(msg);
}

static void signal_mute(void *arg)
{
	dbus_uint32_t serial = 0;
	DBusMessage *msg;
	dbus_bool_t muted;

	msg = create_dbus_message("SystemMuteChanged");
	if (!msg)
		return;

	muted = cras_system_get_mute();
	dbus_message_append_args(msg,
				 DBUS_TYPE_BOOLEAN, &muted,
				 DBUS_TYPE_INVALID);
	dbus_connection_send(dbus_control.conn, msg, &serial);
	dbus_message_unref(msg);
}

static void signal_capture_gain(void *arg)
{
	dbus_uint32_t serial = 0;
	DBusMessage *msg;
	dbus_int32_t gain;

	msg = create_dbus_message("SystemCaptureGainChanged");
	if (!msg)
		return;

	gain = cras_system_get_capture_gain();
	dbus_message_append_args(msg,
				 DBUS_TYPE_INT32, &gain,
				 DBUS_TYPE_INVALID);
	dbus_connection_send(dbus_control.conn, msg, &serial);
	dbus_message_unref(msg);
}

static void signal_capture_mute(void *arg)
{
	dbus_uint32_t serial = 0;
	DBusMessage *msg;
	dbus_bool_t muted;

	msg = create_dbus_message("SystemCaptureMuteChanged");
	if (!msg)
		return;

	muted = cras_system_get_capture_mute();
	dbus_message_append_args(msg,
				 DBUS_TYPE_BOOLEAN, &muted,
				 DBUS_TYPE_INVALID);
	dbus_connection_send(dbus_control.conn, msg, &serial);
	dbus_message_unref(msg);
}

/* Exported Interface */

#define MAX_MEMBER_LEN 64
static const char base_match[] =
	"type='method_call',interface='org.chromium.cras'";
static const unsigned MAX_MATCH_LEN = ARRAY_SIZE(base_match) + MAX_MEMBER_LEN;

struct match_method {
	char member[MAX_MEMBER_LEN];
	DBusHandlerResult (*handler)(DBusConnection *,
				     DBusMessage *,
				     void *);
};
static struct match_method match_methods[] = {
	{"SetSystemVolume", handle_set_system_volume},
	{"SetSystemMute", handle_set_system_mute},
	{"SetSystemCaptureGain", handle_set_system_capture_gain},
	{"SetSystemCaptureMute", handle_set_system_capture_mute},
	{"GetSystemVolumeState", handle_get_system_volume_state},
};

void cras_dbus_control_start(DBusConnection *conn)
{
	char *match_str;
	int i;

	dbus_control.conn = conn;
	dbus_connection_ref(dbus_control.conn);

	match_str = malloc(MAX_MATCH_LEN);

	for (i = 0; i < ARRAY_SIZE(match_methods); i++) {
		snprintf(match_str, MAX_MATCH_LEN,
			 "%s,member='%s'", base_match,
			 match_methods[i].member);
		add_dbus_match(match_str, match_methods[i].handler);
	}

	free(match_str);

	cras_system_register_volume_changed_cb(signal_volume, 0);
	cras_system_register_mute_changed_cb(signal_mute, 0);
	cras_system_register_capture_gain_changed_cb(signal_capture_gain, 0);
	cras_system_register_capture_mute_changed_cb(signal_capture_mute, 0);
}

void cras_dbus_control_stop()
{
	char *match_str;
	int i;

	if (!dbus_control.conn)
		return;

	cras_system_remove_volume_changed_cb(signal_volume, 0);
	cras_system_remove_mute_changed_cb(signal_mute, 0);
	cras_system_remove_capture_gain_changed_cb(signal_capture_gain, 0);
	cras_system_remove_capture_mute_changed_cb(signal_capture_mute, 0);

	match_str = malloc(MAX_MATCH_LEN);

	for (i = 0; i < ARRAY_SIZE(match_methods); i++) {
		snprintf(match_str, MAX_MATCH_LEN,
			 "%s,member='%s'", base_match,
			 match_methods[i].member);
		remove_dbus_match(match_str, match_methods[i].handler);
	}

	free(match_str);

	dbus_connection_unref(dbus_control.conn);
	dbus_control.conn = NULL;
}
