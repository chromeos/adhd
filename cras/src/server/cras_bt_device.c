/* Copyright (c) 2013 The Chromium Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <dbus/dbus.h>

#include <errno.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <syslog.h>

#include "cras_bt_adapter.h"
#include "cras_bt_device.h"
#include "cras_bt_constants.h"
#include "utlist.h"


struct cras_bt_device {
	char *object_path;
	struct cras_bt_adapter *adapter;
	char *address;
	char *name;
	uint32_t bluetooth_class;
	int paired;
	int trusted;
	int connected;
	enum cras_bt_device_profile profiles;

	struct cras_bt_device *prev, *next;
};

static struct cras_bt_device *devices;


enum cras_bt_device_profile cras_bt_device_profile_from_uuid(const char *uuid)
{
	if (strcmp(uuid, HFP_HS_UUID) == 0)
		return CRAS_BT_DEVICE_PROFILE_HEADSET;
	else if (strcmp(uuid, HFP_AG_UUID) == 0)
		return CRAS_BT_DEVICE_PROFILE_HEADSET_GATEWAY;
	else if (strcmp(uuid, A2DP_SOURCE_UUID) == 0)
		return CRAS_BT_DEVICE_PROFILE_A2DP_SOURCE;
	else if (strcmp(uuid, A2DP_SINK_UUID) == 0)
		return CRAS_BT_DEVICE_PROFILE_A2DP_SINK;
	else if (strcmp(uuid, AVRCP_REMOTE_UUID) == 0)
		return CRAS_BT_DEVICE_PROFILE_AVRCP_REMOTE;
	else if (strcmp(uuid, AVRCP_TARGET_UUID) == 0)
		return CRAS_BT_DEVICE_PROFILE_AVRCP_TARGET;
	else
		return 0;
}

struct cras_bt_device *cras_bt_device_create(const char *object_path)
{
	struct cras_bt_device *device;

	device = calloc(1, sizeof(*device));
	if (device == NULL)
		return NULL;

	device->object_path = strdup(object_path);
	if (device->object_path == NULL) {
		free(device);
		return NULL;
	}

	DL_APPEND(devices, device);

	return device;
}

void cras_bt_device_destroy(struct cras_bt_device *device)
{
	DL_DELETE(devices, device);

	free(device->object_path);
	free(device->address);
	free(device->name);
	free(device);
}

void cras_bt_device_reset()
{
	while (devices) {
		syslog(LOG_INFO, "Bluetooth Device: %s removed",
		       devices->address);
		cras_bt_device_destroy(devices);
	}
}


struct cras_bt_device *cras_bt_device_get(const char *object_path)
{
	struct cras_bt_device *device;

	DL_FOREACH(devices, device) {
		if (strcmp(device->object_path, object_path) == 0)
			return device;
	}

	return NULL;
}

size_t cras_bt_device_get_list(struct cras_bt_device ***device_list_out)
{
	struct cras_bt_device *device;
	struct cras_bt_device **device_list = NULL;
	size_t num_devices = 0;

	DL_FOREACH(devices, device) {
		struct cras_bt_device **tmp;

		tmp = realloc(device_list,
			      sizeof(device_list[0]) * (num_devices + 1));
		if (!tmp) {
			free(device_list);
			return -ENOMEM;
		}

		device_list = tmp;
		device_list[num_devices++] = device;
	}

	*device_list_out = device_list;
	return num_devices;
}

const char *cras_bt_device_object_path(const struct cras_bt_device *device)
{
	return device->object_path;
}

struct cras_bt_adapter *cras_bt_device_adapter(
	const struct cras_bt_device *device)
{
	return device->adapter;
}

const char *cras_bt_device_address(const struct cras_bt_device *device)
{
	return device->address;
}

const char *cras_bt_device_name(const struct cras_bt_device *device)
{
	return device->name;
}

int cras_bt_device_paired(const struct cras_bt_device *device)
{
	return device->paired;
}

int cras_bt_device_trusted(const struct cras_bt_device *device)
{
	return device->trusted;
}

int cras_bt_device_connected(const struct cras_bt_device *device)
{
	return device->connected;
}

int cras_bt_device_supports_profile(const struct cras_bt_device *device,
				    enum cras_bt_device_profile profile)
{
	return device->profiles & profile;
}


static void cras_bt_device_log_profile(const struct cras_bt_device *device,
				       enum cras_bt_device_profile profile)
{
	switch (profile) {
	case CRAS_BT_DEVICE_PROFILE_HEADSET:
		syslog(LOG_DEBUG, "Bluetooth Device: %s is Headset",
		       device->address);
		break;
	case CRAS_BT_DEVICE_PROFILE_HEADSET_GATEWAY:
		syslog(LOG_DEBUG, "Bluetooth Device: %s is Headset gateway",
		       device->address);
		break;
	case CRAS_BT_DEVICE_PROFILE_A2DP_SOURCE:
		syslog(LOG_DEBUG, "Bluetooth Device: %s is A2DP source",
		       device->address);
		break;
	case CRAS_BT_DEVICE_PROFILE_A2DP_SINK:
		syslog(LOG_DEBUG, "Bluetooth Device: %s is A2DP sink",
		       device->address);
		break;
	case CRAS_BT_DEVICE_PROFILE_AVRCP_REMOTE:
		syslog(LOG_DEBUG, "Bluetooth Device: %s is AVRCP remote",
		       device->address);
		break;
	case CRAS_BT_DEVICE_PROFILE_AVRCP_TARGET:
		syslog(LOG_DEBUG, "Bluetooth Device: %s is AVRCP target",
		       device->address);
		break;
	}
}

void cras_bt_device_update_properties(struct cras_bt_device *device,
				      DBusMessageIter *properties_array_iter,
				      DBusMessageIter *invalidated_array_iter)
{
	while (dbus_message_iter_get_arg_type(properties_array_iter) !=
	       DBUS_TYPE_INVALID) {
		DBusMessageIter properties_dict_iter, variant_iter;
		const char *key;
		int type;

		dbus_message_iter_recurse(properties_array_iter,
					  &properties_dict_iter);

		dbus_message_iter_get_basic(&properties_dict_iter, &key);
		dbus_message_iter_next(&properties_dict_iter);

		dbus_message_iter_recurse(&properties_dict_iter, &variant_iter);
		type = dbus_message_iter_get_arg_type(&variant_iter);

		if (type == DBUS_TYPE_STRING) {
			const char *value;

			dbus_message_iter_get_basic(&variant_iter, &value);

			if (strcmp(key, "Adapter") == 0) {
				device->adapter = cras_bt_adapter_get(value);

			} else if (strcmp(key, "Address") == 0) {
				free(device->address);
				device->address = strdup(value);

			} else if (strcmp(key, "Alias") == 0) {
				free(device->name);
				device->name = strdup(value);

			}

		} else if (type == DBUS_TYPE_UINT32) {
			uint32_t value;

			dbus_message_iter_get_basic(&variant_iter, &value);

			if (strcmp(key, "Class") == 0)
				device->bluetooth_class = value;

		} else if (type == DBUS_TYPE_BOOLEAN) {
			int value;

			dbus_message_iter_get_basic(&variant_iter, &value);

			if (strcmp(key, "Paired") == 0)
				device->paired = value;
			else if (strcmp(key, "Trusted") == 0)
				device->trusted = value;
			else if (strcmp(key, "Connected") == 0)
				device->connected = value;

		} else if (strcmp(
				dbus_message_iter_get_signature(&variant_iter),
				"as") == 0 &&
			   strcmp(key, "UUIDs") == 0) {
			DBusMessageIter uuid_array_iter;

			dbus_message_iter_recurse(&variant_iter,
						  &uuid_array_iter);
			while (dbus_message_iter_get_arg_type(
				       &uuid_array_iter) != DBUS_TYPE_INVALID) {
				const char *uuid;
				enum cras_bt_device_profile profile;

				dbus_message_iter_get_basic(&uuid_array_iter,
							    &uuid);
				profile = cras_bt_device_profile_from_uuid(
					uuid);

				device->profiles |= profile;
				cras_bt_device_log_profile(device, profile);

				dbus_message_iter_next(&uuid_array_iter);
			}
		}

		dbus_message_iter_next(properties_array_iter);
	}

	while (invalidated_array_iter &&
	       dbus_message_iter_get_arg_type(invalidated_array_iter) !=
	       DBUS_TYPE_INVALID) {
		const char *key;

		dbus_message_iter_get_basic(invalidated_array_iter, &key);

		if (strcmp(key, "Adapter") == 0) {
			device->adapter = NULL;
		} else if (strcmp(key, "Address") == 0) {
			free(device->address);
			device->address = NULL;
		} else if (strcmp(key, "Alias") == 0) {
			free(device->name);
			device->name = NULL;
		} else if (strcmp(key, "Class") == 0) {
			device->bluetooth_class = 0;
		} else if (strcmp(key, "Paired") == 0) {
			device->paired = 0;
		} else if (strcmp(key, "Trusted") == 0) {
			device->trusted = 0;
		} else if (strcmp(key, "Connected") == 0) {
			device->connected = 0;
		} else if (strcmp(key, "UUIDs") == 0) {
			device->profiles = 0;
		}

		dbus_message_iter_next(invalidated_array_iter);
	}
}
