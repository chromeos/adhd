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
#include "cras_iodev_list.h"
#include "cras_system_state.h"
#include "cras_util.h"
#include "utlist.h"

#define CRAS_CONTROL_NAME "org.chromium.cras.Control"
#define CRAS_CONTROL_PATH "/org/chromium/cras/Control"
#define CONTROL_INTROSPECT_XML                                             \
    DBUS_INTROSPECT_1_0_XML_DOCTYPE_DECL_NODE                               \
    "<node>\n"                                                              \
    "  <interface name=\""CRAS_CONTROL_NAME"\">\n"                      \
    "    <method name=\"SetOutputVolume\">\n"                              \
    "      <arg name=\"volume\" type=\"y\" direction=\"in\"/>\n"         \
    "    </method>\n"                                                       \
    "    <method name=\"SetOutputMute\">\n"                              \
    "      <arg name=\"muted\" type=\"b\" direction=\"in\"/>\n"         \
    "    </method>\n"                                                       \
    "    <method name=\"SetInputGain\">\n"                              \
    "      <arg name=\"gain\" type=\"i\" direction=\"in\"/>\n"         \
    "    </method>\n"                                                       \
    "    <method name=\"SetInputMute\">\n"                              \
    "      <arg name=\"muted\" type=\"b\" direction=\"in\"/>\n"         \
    "    </method>\n"                                                       \
    "    <method name=\"GetVolumeState\">\n"                           \
    "      <arg name=\"volume\" type=\"y\" direction=\"out\"/>\n"   \
    "      <arg name=\"muted\" type=\"b\" direction=\"out\"/>\n"   \
    "      <arg name=\"capture_gain\" type=\"i\" direction=\"out\"/>\n"   \
    "      <arg name=\"capture_mute\" type=\"b\" direction=\"out\"/>\n"   \
    "    </method>\n"                                                       \
    "    <method name=\"GetNodes\">\n"                                  \
    "      <arg name=\"nodes\" type=\"a{sv}\" direction=\"out\"/>\n"    \
    "    </method>\n"                                                   \
    "    <method name=\"SetActiveOutputNode\">\n"                       \
    "      <arg name=\"node_id\" type=\"t\" direction=\"in\"/>\n"       \
    "    </method>\n"                                                   \
    "    <method name=\"SetActiveInputNode\">\n"                        \
    "      <arg name=\"node_id\" type=\"t\" direction=\"in\"/>\n"       \
    "    </method>\n"                                                   \
    "  </interface>\n"                                                      \
    "  <interface name=\"" DBUS_INTERFACE_INTROSPECTABLE "\">\n"          \
    "    <method name=\"Introspect\">\n"                                    \
    "      <arg name=\"data\" type=\"s\" direction=\"out\"/>\n"             \
    "    </method>\n"                                                       \
    "  </interface>\n"                                                      \
    "</node>\n"

struct cras_dbus_control {
	DBusConnection *conn;
};
static struct cras_dbus_control dbus_control;
static cras_node_id_t last_output, last_input;

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
static DBusHandlerResult handle_set_output_volume(
	DBusConnection *conn,
	DBusMessage *message,
	void *arg)
{
	int rc;
	dbus_int32_t new_vol;

	rc = get_single_arg(message, DBUS_TYPE_INT32, &new_vol);
	if (rc)
		return rc;

	cras_system_set_volume(new_vol);

	send_empty_reply(message);

	return DBUS_HANDLER_RESULT_HANDLED;
}

static DBusHandlerResult handle_set_output_mute(
	DBusConnection *conn,
	DBusMessage *message,
	void *arg)
{
	int rc;
	dbus_bool_t new_mute;

	rc = get_single_arg(message, DBUS_TYPE_BOOLEAN, &new_mute);
	if (rc)
		return rc;

	cras_system_set_mute(new_mute);

	send_empty_reply(message);

	return DBUS_HANDLER_RESULT_HANDLED;
}

static DBusHandlerResult handle_set_input_gain(
	DBusConnection *conn,
	DBusMessage *message,
	void *arg)
{
	int rc;
	dbus_int32_t new_gain;

	rc = get_single_arg(message, DBUS_TYPE_INT32, &new_gain);
	if (rc)
		return rc;

	cras_system_set_capture_gain(new_gain);

	send_empty_reply(message);

	return DBUS_HANDLER_RESULT_HANDLED;
}

static DBusHandlerResult handle_set_input_mute(
	DBusConnection *conn,
	DBusMessage *message,
	void *arg)
{
	int rc;
	dbus_bool_t new_mute;

	rc = get_single_arg(message, DBUS_TYPE_BOOLEAN, &new_mute);
	if (rc)
		return rc;

	cras_system_set_capture_mute(new_mute);

	send_empty_reply(message);

	return DBUS_HANDLER_RESULT_HANDLED;
}

static DBusHandlerResult handle_get_volume_state(
	DBusConnection *conn,
	DBusMessage *message,
	void *arg)
{
	DBusMessage *reply;
	dbus_uint32_t serial = 0;
	dbus_int32_t volume;
	dbus_bool_t muted;
	dbus_int32_t capture_gain;
	dbus_bool_t capture_muted;

	reply = dbus_message_new_method_return(message);

	volume = cras_system_get_volume();
	muted = cras_system_get_mute();
	capture_gain = cras_system_get_capture_gain();
	capture_muted = cras_system_get_capture_mute();

	dbus_message_append_args(reply,
				 DBUS_TYPE_INT32, &volume,
				 DBUS_TYPE_BOOLEAN, &muted,
				 DBUS_TYPE_INT32, &capture_gain,
				 DBUS_TYPE_BOOLEAN, &capture_muted,
				 DBUS_TYPE_INVALID);

	dbus_connection_send(dbus_control.conn, reply, &serial);

	dbus_message_unref(reply);

	return DBUS_HANDLER_RESULT_HANDLED;
}

/* Appends a key-value pair to the dbus message.
 * Args:
 *    key - the key (a string)
 *    type - the type of the value (for example, 'y')
 *    type_string - the type of the value in string form (for example, "y")
 *    value - a pointer to the value to be appended.
 * Returns:
 *    false if not enough memory.
*/
static dbus_bool_t append_key_value(DBusMessageIter *iter, const char *key,
				    int type, const char *type_string,
				    void *value)
{
	DBusMessageIter entry, variant;

	if (!dbus_message_iter_open_container(iter, DBUS_TYPE_DICT_ENTRY, NULL,
					      &entry))
		return FALSE;
	if (!dbus_message_iter_append_basic(&entry, DBUS_TYPE_STRING, &key))
		return FALSE;
	if (!dbus_message_iter_open_container(&entry, DBUS_TYPE_VARIANT,
					      type_string, &variant))
		return FALSE;
	if (!dbus_message_iter_append_basic(&variant, type, value))
		return FALSE;
	if (!dbus_message_iter_close_container(&entry, &variant))
		return FALSE;
	if (!dbus_message_iter_close_container(iter, &entry))
		return FALSE;

	return TRUE;
}

/* Appends the information about a node to the dbus message. Returns
 * false if not enough memory. */
static dbus_bool_t append_node_dict(DBusMessageIter *iter,
				    const struct cras_iodev_info *dev,
				    const struct cras_ionode_info *node,
				    enum CRAS_STREAM_DIRECTION direction)
{
	DBusMessageIter dict;
	dbus_bool_t is_input;
	dbus_uint64_t id;
	const char *dev_name = dev->name;
	const char *node_type = node->type;
	const char *node_name = node->name;
	dbus_bool_t active;

	is_input = (direction == CRAS_STREAM_INPUT);
	id = node->iodev_idx;
	id = (id << 32) | node->ionode_idx;
	active = !!node->active;

	if (!dbus_message_iter_open_container(iter, DBUS_TYPE_ARRAY, "{sv}",
					      &dict))
		return FALSE;
	if (!append_key_value(&dict, "IsInput", DBUS_TYPE_BOOLEAN,
			      DBUS_TYPE_BOOLEAN_AS_STRING, &is_input))
		return FALSE;
	if (!append_key_value(&dict, "Id", DBUS_TYPE_UINT64,
			      DBUS_TYPE_UINT64_AS_STRING, &id))
		return FALSE;
	if (!append_key_value(&dict, "DeviceName", DBUS_TYPE_STRING,
			      DBUS_TYPE_STRING_AS_STRING, &dev_name))
		return FALSE;
	if (!append_key_value(&dict, "Type", DBUS_TYPE_STRING,
			      DBUS_TYPE_STRING_AS_STRING, &node_type))
		return FALSE;
	if (!append_key_value(&dict, "Name", DBUS_TYPE_STRING,
			      DBUS_TYPE_STRING_AS_STRING, &node_name))
		return FALSE;
	if (!append_key_value(&dict, "Active", DBUS_TYPE_BOOLEAN,
			      DBUS_TYPE_BOOLEAN_AS_STRING, &active))
		return FALSE;
	if (!dbus_message_iter_close_container(iter, &dict))
		return FALSE;

	return TRUE;
}

/* Appends the information about all nodes in a given direction. Returns false
 * if not enough memory. */
static dbus_bool_t append_nodes(enum CRAS_STREAM_DIRECTION direction,
				DBusMessageIter *array)
{
	const struct cras_iodev_info *devs;
	const struct cras_ionode_info *nodes;
	int ndevs, nnodes;
	int i, j;

	if (direction == CRAS_STREAM_OUTPUT) {
		ndevs = cras_system_state_get_output_devs(&devs);
		nnodes = cras_system_state_get_output_nodes(&nodes);
	} else {
		ndevs = cras_system_state_get_input_devs(&devs);
		nnodes = cras_system_state_get_input_nodes(&nodes);
	}

	for (i = 0; i < nnodes; i++) {
		/* Don't reply unplugged nodes. */
		if (!nodes[i].plugged)
			continue;
		/* Find the device for this node. */
		for (j = 0; j < ndevs; j++)
			if (devs[j].idx == nodes[i].iodev_idx)
				break;
		if (j == ndevs)
			continue;
		/* Send information about this node. */
		if (!append_node_dict(array, &devs[j], &nodes[i], direction))
			return FALSE;
	}

	return TRUE;
}

static DBusHandlerResult handle_get_nodes(DBusConnection *conn,
					  DBusMessage *message,
					  void *arg)
{
	DBusMessage *reply;
	DBusMessageIter array;
	dbus_uint32_t serial = 0;

	reply = dbus_message_new_method_return(message);
	dbus_message_iter_init_append(reply, &array);
	if (!append_nodes(CRAS_STREAM_OUTPUT, &array))
		return DBUS_HANDLER_RESULT_NEED_MEMORY;
	if (!append_nodes(CRAS_STREAM_INPUT, &array))
		return DBUS_HANDLER_RESULT_NEED_MEMORY;
	dbus_connection_send(dbus_control.conn, reply, &serial);
	dbus_message_unref(reply);

	return DBUS_HANDLER_RESULT_HANDLED;
}

static DBusHandlerResult
handle_set_active_node(DBusConnection *conn,
		       DBusMessage *message,
		       void *arg,
		       enum CRAS_STREAM_DIRECTION direction)
{
	int rc;
	cras_node_id_t id;

	rc = get_single_arg(message, DBUS_TYPE_UINT64, &id);
	if (rc)
		return rc;

	cras_iodev_list_select_node(direction, id);

	send_empty_reply(message);

	return DBUS_HANDLER_RESULT_HANDLED;
}

/* Handle incoming messages. */
static DBusHandlerResult handle_control_message(DBusConnection *conn,
						DBusMessage *message,
						void *arg)
{
	syslog(LOG_DEBUG, "Control message: %s %s %s",
	       dbus_message_get_path(message),
	       dbus_message_get_interface(message),
	       dbus_message_get_member(message));

	if (dbus_message_is_method_call(message,
					DBUS_INTERFACE_INTROSPECTABLE,
					"Introspect")) {
		DBusMessage *reply;
		const char *xml = CONTROL_INTROSPECT_XML;

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
					       CRAS_CONTROL_NAME,
					       "SetOutputVolume")) {
		return handle_set_output_volume(conn, message, arg);
	} else if (dbus_message_is_method_call(message,
					       CRAS_CONTROL_NAME,
					       "SetOutputMute")) {
		return handle_set_output_mute(conn, message, arg);
	} else if (dbus_message_is_method_call(message,
					       CRAS_CONTROL_NAME,
					       "SetInputGain")) {
		return handle_set_input_gain(conn, message, arg);
	} else if (dbus_message_is_method_call(message,
					       CRAS_CONTROL_NAME,
					       "SetInputMute")) {
		return handle_set_input_mute(conn, message, arg);
	} else if (dbus_message_is_method_call(message,
					       CRAS_CONTROL_NAME,
					       "GetVolumeState")) {
		return handle_get_volume_state(conn, message, arg);
	} else if (dbus_message_is_method_call(message,
					       CRAS_CONTROL_NAME,
					       "GetNodes")) {
		return handle_get_nodes(conn, message, arg);
	} else if (dbus_message_is_method_call(message,
					       CRAS_CONTROL_NAME,
					       "SetActiveOutputNode")) {
		return handle_set_active_node(conn, message, arg,
					      CRAS_STREAM_OUTPUT);
	} else if (dbus_message_is_method_call(message,
					       CRAS_CONTROL_NAME,
					       "SetActiveInputNode")) {
		return handle_set_active_node(conn, message, arg,
					      CRAS_STREAM_INPUT);
	}

	return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
}

/* Creates a new DBus message, must be freed with dbus_message_unref. */
static DBusMessage *create_dbus_message(const char *name)
{
	DBusMessage *msg;
	msg = dbus_message_new_signal(CRAS_CONTROL_PATH,
				      CRAS_CONTROL_NAME,
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
	dbus_int32_t volume;

	msg = create_dbus_message("OutputVolumeChanged");
	if (!msg)
		return;

	volume = cras_system_get_volume();
	dbus_message_append_args(msg,
				 DBUS_TYPE_INT32, &volume,
				 DBUS_TYPE_INVALID);
	dbus_connection_send(dbus_control.conn, msg, &serial);
	dbus_message_unref(msg);
}

static void signal_mute(void *arg)
{
	dbus_uint32_t serial = 0;
	DBusMessage *msg;
	dbus_bool_t muted;

	msg = create_dbus_message("OutputMuteChanged");
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

	msg = create_dbus_message("InputGainChanged");
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

	msg = create_dbus_message("InputMuteChanged");
	if (!msg)
		return;

	muted = cras_system_get_capture_mute();
	dbus_message_append_args(msg,
				 DBUS_TYPE_BOOLEAN, &muted,
				 DBUS_TYPE_INVALID);
	dbus_connection_send(dbus_control.conn, msg, &serial);
	dbus_message_unref(msg);
}

static void signal_nodes_changed(void *arg)
{
	dbus_uint32_t serial = 0;
	DBusMessage *msg;

	msg = create_dbus_message("NodesChanged");
	if (!msg)
		return;

	dbus_connection_send(dbus_control.conn, msg, &serial);
	dbus_message_unref(msg);
}

static void signal_with_node_id(const char *name, cras_node_id_t id)
{
	DBusMessage *msg;
	dbus_uint32_t serial = 0;

	msg = create_dbus_message(name);
	if (!msg)
		return;
	dbus_message_append_args(msg,
				 DBUS_TYPE_UINT64, &id,
				 DBUS_TYPE_INVALID);
	dbus_connection_send(dbus_control.conn, msg, &serial);
	dbus_message_unref(msg);
}

static void signal_active_node_changed(void *arg)
{
	cras_node_id_t output, input;

	output = cras_iodev_list_get_active_node_id(CRAS_STREAM_OUTPUT);
	input = cras_iodev_list_get_active_node_id(CRAS_STREAM_INPUT);

	if (last_output != output) {
		last_output = output;
		signal_with_node_id("ActiveOutputNodeChanged", output);
	}

	if (last_input != input) {
		last_input = input;
		signal_with_node_id("ActiveInputNodeChanged", input);
	}
}

/* Exported Interface */

void cras_dbus_control_start(DBusConnection *conn)
{
	static const DBusObjectPathVTable control_vtable = {
		.message_function = handle_control_message,
	};

	DBusError dbus_error;

	dbus_control.conn = conn;
	dbus_connection_ref(dbus_control.conn);

	if (!dbus_connection_register_object_path(conn,
						  CRAS_CONTROL_PATH,
						  &control_vtable,
						  &dbus_error)) {
		syslog(LOG_WARNING,
		       "Couldn't register CRAS control: %s: %s",
		       CRAS_CONTROL_PATH, dbus_error.message);
		dbus_error_free(&dbus_error);
		return;
	}

	cras_system_register_volume_changed_cb(signal_volume, 0);
	cras_system_register_mute_changed_cb(signal_mute, 0);
	cras_system_register_capture_gain_changed_cb(signal_capture_gain, 0);
	cras_system_register_capture_mute_changed_cb(signal_capture_mute, 0);
	cras_iodev_list_register_nodes_changed_cb(signal_nodes_changed, 0);
	cras_iodev_list_register_active_node_changed_cb(
		signal_active_node_changed, 0);
}

void cras_dbus_control_stop()
{
	if (!dbus_control.conn)
		return;

	cras_system_remove_volume_changed_cb(signal_volume, 0);
	cras_system_remove_mute_changed_cb(signal_mute, 0);
	cras_system_remove_capture_gain_changed_cb(signal_capture_gain, 0);
	cras_system_remove_capture_mute_changed_cb(signal_capture_mute, 0);
	cras_iodev_list_remove_nodes_changed_cb(signal_nodes_changed, 0);
	cras_iodev_list_remove_active_node_changed_cb(
		signal_active_node_changed, 0);

	dbus_connection_unregister_object_path(dbus_control.conn,
					       CRAS_CONTROL_PATH);

	dbus_connection_unref(dbus_control.conn);
	dbus_control.conn = NULL;
}
