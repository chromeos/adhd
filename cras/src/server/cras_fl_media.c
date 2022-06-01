/* Copyright 2021 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "dbus/dbus-protocol.h"
#include <dbus/dbus.h>
#include <errno.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <syslog.h>

#include "cras_a2dp_manager.h"
#include "cras_bt_io.h"
#include "cras_bt_policy.h"
#include "cras_fl_manager.h"
#include "cras_fl_media.h"
#include "cras_hfp_manager.h"
#include "utlist.h"

#define BT_SERVICE_NAME "org.chromium.bluetooth"
/* Object path is of the form BT_OBJECT_BASE + hci + BT_OBJECT_MEDIA */
#define BT_OBJECT_BASE "/org/chromium/bluetooth/hci"
#define BT_OBJECT_MEDIA "/media"
#define BT_MEDIA_INTERFACE "org.chromium.bluetooth.BluetoothMedia"

#define BT_MEDIA_CALLBACK_INTERFACE                                            \
	"org.chromium.bluetooth.BluetoothMediaCallback"

#define CRAS_BT_MEDIA_OBJECT_PATH "/org/chromium/cras/bluetooth/media"
#define BT_MEDIA_OBJECT_PATH_SIZE_MAX 128

/* Hold information and focus on logic related to communicate with the
 * Bluetooth stack through DBus. Information and logic regarding A2DP and
 * AVRCP should be kept in the cras_a2dp for responsibility division.
 * Members:
 *    hci - The id of HCI interface to use.
 *    obj_path - Object path of the Bluetooth media.
 *    conn - The DBus connection object used to send message to Floss Media
 *    interface.
 *    a2dp - Object representing the connected A2DP headset.
 *    hfp - Object representing the connected HFP headset.
 */
struct fl_media {
	unsigned int hci;
	char obj_path[BT_MEDIA_OBJECT_PATH_SIZE_MAX];
	DBusConnection *conn;
	struct cras_a2dp *a2dp;
	struct cras_hfp *hfp;
	struct bt_io_manager *bt_io_mgr;
};

static struct fl_media *active_fm = NULL;

struct fl_media *fl_media_create(int hci)
{
	struct fl_media *fm = (struct fl_media *)calloc(1, sizeof(*fm));

	if (fm == NULL)
		return NULL;
	fm->hci = hci;
	snprintf(fm->obj_path, BT_MEDIA_OBJECT_PATH_SIZE_MAX, "%s%d%s",
		 BT_OBJECT_BASE, hci, BT_OBJECT_MEDIA);
	return fm;
}

/* helper to extract a single argument from a DBus message. */
static int get_single_arg(DBusMessage *message, int dbus_type, void *arg)
{
	DBusError dbus_error;

	dbus_error_init(&dbus_error);

	if (!dbus_message_get_args(message, &dbus_error, dbus_type, arg,
				   DBUS_TYPE_INVALID)) {
		syslog(LOG_WARNING, "Bad method received: %s",
		       dbus_error.message);
		dbus_error_free(&dbus_error);
		return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
	}

	return 0;
}

int floss_media_hfp_set_active_device(struct fl_media *fm, const char *addr)
{
	return 0;
}

int floss_media_hfp_start_sco_call(struct fl_media *fm, const char *addr)
{
	DBusMessage *method_call, *reply;
	DBusError dbus_error;

	syslog(LOG_DEBUG, "floss_media_hfp_start_sco_call: %s", addr);

	if (!fm) {
		syslog(LOG_WARNING, "%s: Floss media not started", __func__);
		return -EINVAL;
	}

	method_call =
		dbus_message_new_method_call(BT_SERVICE_NAME, fm->obj_path,
					     BT_MEDIA_INTERFACE,
					     "StartScoCall");
	if (!method_call)
		return -ENOMEM;

	if (!dbus_message_append_args(method_call, DBUS_TYPE_STRING, &addr,
				      DBUS_TYPE_INVALID)) {
		dbus_message_unref(method_call);
		return -ENOMEM;
	}

	dbus_error_init(&dbus_error);
	reply = dbus_connection_send_with_reply_and_block(
		fm->conn, method_call, DBUS_TIMEOUT_USE_DEFAULT, &dbus_error);
	if (!reply) {
		syslog(LOG_ERR, "Failed to send StartScoCall: %s",
		       dbus_error.message);
		dbus_error_free(&dbus_error);
		dbus_message_unref(method_call);
		return -EIO;
	}

	dbus_message_unref(method_call);

	if (dbus_message_get_type(reply) == DBUS_MESSAGE_TYPE_ERROR) {
		syslog(LOG_ERR, "StartScoCall returned error: %s",
		       dbus_message_get_error_name(reply));
		dbus_message_unref(reply);
		return -EIO;
	}
	return 0;
}

int floss_media_hfp_stop_sco_call(struct fl_media *fm, const char *addr)
{
	DBusMessage *method_call, *reply;
	DBusError dbus_error;

	syslog(LOG_DEBUG, "floss_media_hfp_stop_sco_call");

	method_call =
		dbus_message_new_method_call(BT_SERVICE_NAME, fm->obj_path,
					     BT_MEDIA_INTERFACE, "StopScoCall");
	if (!method_call)
		return -ENOMEM;

	if (!dbus_message_append_args(method_call, DBUS_TYPE_STRING, &addr,
				      DBUS_TYPE_INVALID)) {
		dbus_message_unref(method_call);
		return -ENOMEM;
	}

	dbus_error_init(&dbus_error);

	reply = dbus_connection_send_with_reply_and_block(
		fm->conn, method_call, DBUS_TIMEOUT_USE_DEFAULT, &dbus_error);
	if (!reply) {
		syslog(LOG_ERR, "Failed to send StopScoCall: %s",
		       dbus_error.message);
		dbus_error_free(&dbus_error);
		dbus_message_unref(method_call);
		return -EIO;
	}

	dbus_message_unref(method_call);

	if (dbus_message_get_type(reply) == DBUS_MESSAGE_TYPE_ERROR) {
		syslog(LOG_ERR, "StopScoCall returned error: %s",
		       dbus_message_get_error_name(reply));
		dbus_message_unref(reply);
		return -EIO;
	}
	return 0;
}

int floss_media_a2dp_set_active_device(struct fl_media *fm, const char *addr)
{
	DBusMessage *method_call, *reply;
	DBusError dbus_error;

	syslog(LOG_DEBUG, "floss_media_set_active_device");

	method_call =
		dbus_message_new_method_call(BT_SERVICE_NAME, fm->obj_path,
					     BT_MEDIA_INTERFACE,
					     "SetActiveDevice");
	if (!method_call)
		return -ENOMEM;

	if (!dbus_message_append_args(method_call, DBUS_TYPE_STRING, &addr,
				      DBUS_TYPE_INVALID)) {
		dbus_message_unref(method_call);
		return -ENOMEM;
	}

	dbus_error_init(&dbus_error);
	reply = dbus_connection_send_with_reply_and_block(
		active_fm->conn, method_call, DBUS_TIMEOUT_USE_DEFAULT,
		&dbus_error);
	if (!reply) {
		syslog(LOG_ERR, "Failed to send SetActiveDevice %s: %s", addr,
		       dbus_error.message);
		dbus_error_free(&dbus_error);
		dbus_message_unref(method_call);
		return -EIO;
	}

	dbus_message_unref(method_call);

	if (dbus_message_get_type(reply) == DBUS_MESSAGE_TYPE_ERROR) {
		syslog(LOG_ERR, "SetActiveDevice returned error: %s",
		       dbus_message_get_error_name(reply));
		dbus_message_unref(reply);
		return -EIO;
	}
	return 0;
}

int floss_media_a2dp_set_audio_config(struct fl_media *fm, unsigned int rate,
				      unsigned int bps, unsigned int channels)
{
	DBusMessage *method_call, *reply;
	DBusError dbus_error;
	dbus_uint32_t sample_rate = rate;
	dbus_uint32_t bits_per_sample = bps;
	dbus_uint32_t channel_mode = channels;

	syslog(LOG_DEBUG, "floss_media_a2dp_set_audio_config");

	method_call =
		dbus_message_new_method_call(BT_SERVICE_NAME, fm->obj_path,
					     BT_MEDIA_INTERFACE,
					     "SetAudioConfig");
	if (!method_call)
		return -ENOMEM;

	if (!dbus_message_append_args(method_call, DBUS_TYPE_INT32,
				      &sample_rate, DBUS_TYPE_INVALID)) {
		dbus_message_unref(method_call);
		return -ENOMEM;
	}
	if (!dbus_message_append_args(method_call, DBUS_TYPE_INT32,
				      &bits_per_sample, DBUS_TYPE_INVALID)) {
		dbus_message_unref(method_call);
		return -ENOMEM;
	}
	if (!dbus_message_append_args(method_call, DBUS_TYPE_INT32,
				      &channel_mode, DBUS_TYPE_INVALID)) {
		dbus_message_unref(method_call);
		return -ENOMEM;
	}

	dbus_error_init(&dbus_error);
	reply = dbus_connection_send_with_reply_and_block(
		fm->conn, method_call, DBUS_TIMEOUT_USE_DEFAULT, &dbus_error);
	if (!reply) {
		syslog(LOG_ERR, "Failed to send SetAudioConfig: %s",
		       dbus_error.message);
		dbus_error_free(&dbus_error);
		dbus_message_unref(method_call);
		return -EIO;
	}

	dbus_message_unref(method_call);

	if (dbus_message_get_type(reply) == DBUS_MESSAGE_TYPE_ERROR) {
		syslog(LOG_ERR, "SetAudioConfig returned error: %s",
		       dbus_message_get_error_name(reply));
		dbus_message_unref(reply);
		return -EIO;
	}
	return 0;
}

int floss_media_a2dp_start_audio_request(struct fl_media *fm)
{
	DBusMessage *method_call, *reply;
	DBusError dbus_error;

	syslog(LOG_DEBUG, "floss_media_a2dp_start_audio_request");

	if (!fm) {
		syslog(LOG_WARNING, "%s: Floss media not started", __func__);
		return -EINVAL;
	}

	method_call =
		dbus_message_new_method_call(BT_SERVICE_NAME, fm->obj_path,
					     BT_MEDIA_INTERFACE,
					     "StartAudioRequest");
	if (!method_call)
		return -ENOMEM;

	dbus_error_init(&dbus_error);
	reply = dbus_connection_send_with_reply_and_block(
		fm->conn, method_call, DBUS_TIMEOUT_USE_DEFAULT, &dbus_error);
	if (!reply) {
		syslog(LOG_ERR, "Failed to send StartAudioRequest: %s",
		       dbus_error.message);
		dbus_error_free(&dbus_error);
		dbus_message_unref(method_call);
		return -EIO;
	}

	dbus_message_unref(method_call);

	if (dbus_message_get_type(reply) == DBUS_MESSAGE_TYPE_ERROR) {
		syslog(LOG_ERR, "StartAudioRequest returned error: %s",
		       dbus_message_get_error_name(reply));
		dbus_message_unref(reply);
		return -EIO;
	}
	return 0;
}

int floss_media_a2dp_stop_audio_request(struct fl_media *fm)
{
	DBusMessage *method_call, *reply;
	DBusError dbus_error;

	syslog(LOG_DEBUG, "floss_media_a2dp_stop_audio_request");

	method_call =
		dbus_message_new_method_call(BT_SERVICE_NAME, fm->obj_path,
					     BT_MEDIA_INTERFACE,
					     "StopAudioRequest");
	if (!method_call)
		return -ENOMEM;

	dbus_error_init(&dbus_error);

	reply = dbus_connection_send_with_reply_and_block(
		fm->conn, method_call, DBUS_TIMEOUT_USE_DEFAULT, &dbus_error);
	if (!reply) {
		syslog(LOG_ERR, "Failed to send StopAudioRequest: %s",
		       dbus_error.message);
		dbus_error_free(&dbus_error);
		dbus_message_unref(method_call);
		return -EIO;
	}

	dbus_message_unref(method_call);

	if (dbus_message_get_type(reply) == DBUS_MESSAGE_TYPE_ERROR) {
		syslog(LOG_ERR, "StopAudioRequest returned error: %s",
		       dbus_message_get_error_name(reply));
		dbus_message_unref(reply);
		return -EIO;
	}
	return 0;
}

int floss_media_a2dp_suspend(struct fl_media *fm)
{
	if (fm != active_fm) {
		syslog(LOG_WARNING,
		       "Invalid fl_media instance to suspend a2dp");
		return 0;
	}

	if (fm->a2dp == NULL) {
		syslog(LOG_WARNING, "Invalid a2dp instance to suspend");
		return 0;
	}

	bt_io_manager_remove_iodev(active_fm->bt_io_mgr,
				   cras_floss_a2dp_get_iodev(active_fm->a2dp));
	cras_floss_a2dp_destroy(active_fm->a2dp);
	active_fm->a2dp = NULL;
	return 0;
}

static bool get_presentation_position_result(DBusMessage *message,
					     uint64_t *remote_delay_report_ns,
					     uint64_t *total_bytes_read,
					     struct timespec *data_position_ts)
{
	DBusMessageIter iter, dict;
	dbus_uint64_t bytes;
	dbus_uint64_t delay_ns;
	dbus_int64_t data_position_sec;
	dbus_int32_t data_position_nsec;

	dbus_message_iter_init(message, &iter);
	if (dbus_message_iter_get_arg_type(&iter) != DBUS_TYPE_ARRAY) {
		syslog(LOG_ERR, "GetPresentationPosition returned not array");
		return false;
	}

	dbus_message_iter_recurse(&iter, &dict);

	while (dbus_message_iter_get_arg_type(&dict) != DBUS_TYPE_INVALID) {
		DBusMessageIter entry, var;
		const char *key;

		if (dbus_message_iter_get_arg_type(&dict) !=
		    DBUS_TYPE_DICT_ENTRY) {
			syslog(LOG_ERR, "entry not dictionary");
			return FALSE;
		}

		dbus_message_iter_recurse(&dict, &entry);
		if (dbus_message_iter_get_arg_type(&entry) !=
		    DBUS_TYPE_STRING) {
			syslog(LOG_ERR, "entry not string");
			return FALSE;
		}

		dbus_message_iter_get_basic(&entry, &key);
		dbus_message_iter_next(&entry);

		if (dbus_message_iter_get_arg_type(&entry) != DBUS_TYPE_VARIANT)
			return FALSE;

		dbus_message_iter_recurse(&entry, &var);
		if (strcasecmp(key, "total_bytes_read") == 0) {
			if (dbus_message_iter_get_arg_type(&var) !=
			    DBUS_TYPE_UINT64)
				return FALSE;

			dbus_message_iter_get_basic(&var, &bytes);
		} else if (strcasecmp(key, "remote_delay_report_ns") == 0) {
			if (dbus_message_iter_get_arg_type(&var) !=
			    DBUS_TYPE_UINT64)
				return FALSE;

			dbus_message_iter_get_basic(&var, &delay_ns);
		} else if (strcasecmp(key, "remote_delay_report_ns") == 0) {
			if (dbus_message_iter_get_arg_type(&var) !=
			    DBUS_TYPE_UINT64)
				return FALSE;

			dbus_message_iter_get_basic(&var,
						    &remote_delay_report_ns);
		} else if (strcasecmp(key, "data_position_sec") == 0) {
			if (dbus_message_iter_get_arg_type(&var) !=
			    DBUS_TYPE_INT64)
				return FALSE;

			dbus_message_iter_get_basic(&var, &data_position_sec);
		} else if (strcasecmp(key, "data_position_nsec") == 0) {
			if (dbus_message_iter_get_arg_type(&var) !=
			    DBUS_TYPE_INT32)
				return FALSE;

			dbus_message_iter_get_basic(&var, &data_position_nsec);
		} else
			syslog(LOG_WARNING, "%s not supported, ignoring", key);

		dbus_message_iter_next(&dict);
	}

	*total_bytes_read = bytes;
	*remote_delay_report_ns = delay_ns;
	data_position_ts->tv_sec = data_position_sec;
	data_position_ts->tv_nsec = data_position_nsec;
	return true;
}

int floss_media_a2dp_get_presentation_position(
	struct fl_media *fm, uint64_t *remote_delay_report_ns,
	uint64_t *total_bytes_read, struct timespec *data_position_ts)
{
	DBusMessage *method_call, *reply;
	DBusError dbus_error;

	method_call =
		dbus_message_new_method_call(BT_SERVICE_NAME, fm->obj_path,
					     BT_MEDIA_INTERFACE,
					     "GetPresentationPosition");
	if (!method_call)
		return -ENOMEM;

	dbus_error_init(&dbus_error);

	reply = dbus_connection_send_with_reply_and_block(
		fm->conn, method_call, DBUS_TIMEOUT_USE_DEFAULT, &dbus_error);
	if (!reply) {
		syslog(LOG_ERR, "Failed to send GetPresentationPosition: %s",
		       dbus_error.message);
		dbus_error_free(&dbus_error);
		dbus_message_unref(method_call);
		return -EIO;
	}

	dbus_message_unref(method_call);

	if (dbus_message_get_type(reply) == DBUS_MESSAGE_TYPE_ERROR) {
		syslog(LOG_ERR, "GetPresentationPosition returned error: %s",
		       dbus_message_get_error_name(reply));
		dbus_message_unref(reply);
		return -EIO;
	}

	if (!get_presentation_position_result(reply, remote_delay_report_ns,
					      total_bytes_read,
					      data_position_ts)) {
		syslog(LOG_ERR,
		       "GetPresentationPosition returned invalid results");
		dbus_message_unref(reply);
		return -EIO;
	}

	return 0;
}

int floss_media_a2dp_set_volume(struct fl_media *fm, unsigned int volume)
{
	DBusMessage *method_call, *reply;
	DBusError dbus_error;
	dbus_int32_t absolute_volume = volume;

	syslog(LOG_DEBUG, "floss_media_a2dp_set_volume: %d", absolute_volume);

	method_call = dbus_message_new_method_call(
		BT_SERVICE_NAME, fm->obj_path, BT_MEDIA_INTERFACE, "SetVolume");
	if (!method_call)
		return -ENOMEM;

	if (!dbus_message_append_args(method_call, DBUS_TYPE_INT32,
				      &absolute_volume, DBUS_TYPE_INVALID)) {
		dbus_message_unref(method_call);
		return -ENOMEM;
	}

	dbus_error_init(&dbus_error);

	reply = dbus_connection_send_with_reply_and_block(
		fm->conn, method_call, DBUS_TIMEOUT_USE_DEFAULT, &dbus_error);
	if (!reply) {
		syslog(LOG_ERR, "Failed to send SetVolume: %s",
		       dbus_error.message);
		dbus_error_free(&dbus_error);
		dbus_message_unref(method_call);
		return -EIO;
	}

	dbus_message_unref(method_call);

	if (dbus_message_get_type(reply) == DBUS_MESSAGE_TYPE_ERROR) {
		syslog(LOG_ERR, "SetVolume returned error: %s",
		       dbus_message_get_error_name(reply));
		dbus_message_unref(reply);
	}
	return 0;
}

static void floss_on_register_callback(DBusPendingCall *pending_call,
				       void *data)
{
	DBusMessage *reply;

	reply = dbus_pending_call_steal_reply(pending_call);
	dbus_pending_call_unref(pending_call);

	if (dbus_message_get_type(reply) == DBUS_MESSAGE_TYPE_ERROR) {
		syslog(LOG_WARNING, "RegisterCallback returned error: %s",
		       dbus_message_get_error_name(reply));
		dbus_message_unref(reply);
		return;
	}
	dbus_message_unref(reply);
}

static int floss_media_register_callback(DBusConnection *conn,
					 const struct fl_media *fm)
{
	const char *bt_media_object_path = CRAS_BT_MEDIA_OBJECT_PATH;
	DBusMessage *method_call;
	DBusPendingCall *pending_call;

	method_call =
		dbus_message_new_method_call(BT_SERVICE_NAME, fm->obj_path,
					     BT_MEDIA_INTERFACE,
					     "RegisterCallback");
	if (!method_call)
		return -ENOMEM;

	if (!dbus_message_append_args(method_call, DBUS_TYPE_OBJECT_PATH,
				      &bt_media_object_path,
				      DBUS_TYPE_INVALID)) {
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
	if (!pending_call)
		return -EIO;

	if (!dbus_pending_call_set_notify(
		    pending_call, floss_on_register_callback, conn, NULL)) {
		dbus_pending_call_cancel(pending_call);
		dbus_pending_call_unref(pending_call);
		return -ENOMEM;
	}

	return 0;
}

static struct cras_fl_a2dp_codec_config *
parse_a2dp_codec(DBusMessageIter *codec)
{
	DBusMessageIter dict;
	const char *key = NULL;
	dbus_int32_t bps, channels, priority, type, rate;
	bps = channels = priority = type = rate = -1;

	dbus_message_iter_recurse(codec, &dict);
	while (dbus_message_iter_get_arg_type(&dict) != DBUS_TYPE_INVALID) {
		DBusMessageIter entry, var;
		if (dbus_message_iter_get_arg_type(&dict) !=
		    DBUS_TYPE_DICT_ENTRY) {
			syslog(LOG_ERR, "entry not dictionary");
			return NULL;
		}

		dbus_message_iter_recurse(&dict, &entry);
		if (dbus_message_iter_get_arg_type(&entry) !=
		    DBUS_TYPE_STRING) {
			syslog(LOG_ERR, "entry not string");
			return NULL;
		}

		dbus_message_iter_get_basic(&entry, &key);
		dbus_message_iter_next(&entry);
		if (dbus_message_iter_get_arg_type(&entry) !=
		    DBUS_TYPE_VARIANT) {
			syslog(LOG_ERR, "entry not variant");
			return NULL;
		}
		dbus_message_iter_recurse(&entry, &var);
		if (strcasecmp(key, "bits_per_sample") == 0) {
			if (dbus_message_iter_get_arg_type(&var) !=
			    DBUS_TYPE_INT32)
				goto invald_dict_value;

			dbus_message_iter_get_basic(&var, &bps);
		} else if (strcasecmp(key, "channel_mode") == 0) {
			if (dbus_message_iter_get_arg_type(&var) !=
			    DBUS_TYPE_INT32)
				goto invald_dict_value;

			dbus_message_iter_get_basic(&var, &channels);
		} else if (strcasecmp(key, "codec_priority") == 0) {
			if (dbus_message_iter_get_arg_type(&var) !=
			    DBUS_TYPE_INT32)
				goto invald_dict_value;

			dbus_message_iter_get_basic(&var, &priority);
		} else if (strcasecmp(key, "codec_type") == 0) {
			if (dbus_message_iter_get_arg_type(&var) !=
			    DBUS_TYPE_INT32)
				goto invald_dict_value;

			dbus_message_iter_get_basic(&var, &type);
		} else if (strcasecmp(key, "sample_rate") == 0) {
			if (dbus_message_iter_get_arg_type(&var) !=
			    DBUS_TYPE_INT32)
				goto invald_dict_value;

			dbus_message_iter_get_basic(&var, &rate);
		} else if (strcasecmp(key, "codec_specific_1") == 0) {
			if (dbus_message_iter_get_arg_type(&var) !=
			    DBUS_TYPE_INT64)
				goto invald_dict_value;

			/* No active use case yet */
		} else if (strcasecmp(key, "codec_specific_2") == 0) {
			if (dbus_message_iter_get_arg_type(&var) !=
			    DBUS_TYPE_INT64)
				goto invald_dict_value;

			/* No active use case yet */
		} else if (strcasecmp(key, "codec_specific_3") == 0) {
			if (dbus_message_iter_get_arg_type(&var) !=
			    DBUS_TYPE_INT64)
				goto invald_dict_value;

			/* No active use case yet */
		} else if (strcasecmp(key, "codec_specific_4") == 0) {
			if (dbus_message_iter_get_arg_type(&var) !=
			    DBUS_TYPE_INT64)
				goto invald_dict_value;

			/* No active use case yet */
		} else {
			syslog(LOG_WARNING, "%s not supported, ignoring", key);
		}

		dbus_message_iter_next(&dict);
	}

	if (bps == -1 || channels == -1 || priority == -1 || type == -1 ||
	    rate == -1) {
		syslog(LOG_WARNING,
		       "Ignore Incomplete a2dp_codec_config: ("
		       "bits_per_sample:%d,"
		       "channel_mode:%d,"
		       "codec_priority:%d,"
		       "codec_type:%d,"
		       "sample_rate:%d)",
		       bps, channels, priority, type, rate);
		return NULL;
	}

	return cras_floss_a2dp_codec_create(bps, channels, priority, type,
					    rate);
invald_dict_value:
	syslog(LOG_ERR, "Invalid value type for key %s", key);
	return NULL;
}

static struct cras_fl_a2dp_codec_config *
parse_a2dp_codecs(DBusMessageIter *codecs_iter)
{
	struct cras_fl_a2dp_codec_config *codecs = NULL, *config;
	DBusMessageIter array_iter;

	dbus_message_iter_recurse(codecs_iter, &array_iter);

	while (dbus_message_iter_get_arg_type(&array_iter) !=
	       DBUS_TYPE_INVALID) {
		config = parse_a2dp_codec(&array_iter);
		if (config) {
			LL_APPEND(codecs, config);
			syslog(LOG_DEBUG,
			       "Parsed a2dp_codec_config: ("
			       "bits_per_sample:%d,"
			       "channel_mode:%d,"
			       "codec_priority:%d,"
			       "codec_type:%d,"
			       "sample_rate:%d)",
			       config->bits_per_sample, config->channel_mode,
			       config->codec_priority, config->codec_type,
			       config->sample_rate);
		}
		dbus_message_iter_next(&array_iter);
	}

	return codecs;
}

static bool parse_bluetooth_audio_device_added(
	DBusMessage *message, const char **addr, const char **name,
	struct cras_fl_a2dp_codec_config **codecs, dbus_int32_t *hfp_cap,
	dbus_bool_t *abs_vol_supported)
{
	DBusMessageIter iter, dict;
	const char *remote_name, *address;

	dbus_message_iter_init(message, &iter);

	if (!dbus_message_has_signature(message, "a{sv}")) {
		syslog(LOG_ERR,
		       "Received wrong format BluetoothAudioDeviceAdded signal");
		return FALSE;
	}

	/* Default to False if not provided. */
	*abs_vol_supported = FALSE;

	dbus_message_iter_recurse(&iter, &dict);
	while (dbus_message_iter_get_arg_type(&dict) != DBUS_TYPE_INVALID) {
		DBusMessageIter entry, var;
		const char *key;

		if (dbus_message_iter_get_arg_type(&dict) !=
		    DBUS_TYPE_DICT_ENTRY)
			return FALSE;

		dbus_message_iter_recurse(&dict, &entry);
		if (dbus_message_iter_get_arg_type(&entry) != DBUS_TYPE_STRING)
			return FALSE;

		dbus_message_iter_get_basic(&entry, &key);
		dbus_message_iter_next(&entry);
		if (dbus_message_iter_get_arg_type(&entry) != DBUS_TYPE_VARIANT)
			return FALSE;

		dbus_message_iter_recurse(&entry, &var);
		if (strcasecmp(key, "name") == 0) {
			if (dbus_message_iter_get_arg_type(&var) !=
			    DBUS_TYPE_STRING)
				return FALSE;

			dbus_message_iter_get_basic(&var, &remote_name);
		} else if (strcasecmp(key, "address") == 0) {
			if (dbus_message_iter_get_arg_type(&var) !=
			    DBUS_TYPE_STRING)
				return FALSE;

			dbus_message_iter_get_basic(&var, &address);
		} else if (strcasecmp(key, "a2dp_caps") == 0) {
			if (dbus_message_iter_get_arg_type(&var) !=
			    DBUS_TYPE_ARRAY)
				return FALSE;

			*codecs = parse_a2dp_codecs(&var);
		} else if (strcasecmp(key, "hfp_cap") == 0) {
			if (dbus_message_iter_get_arg_type(&var) !=
			    DBUS_TYPE_INT32)
				return FALSE;

			dbus_message_iter_get_basic(&var, hfp_cap);
		} else if (strcasecmp(key, "absolute_volume") == 0) {
			if (dbus_message_iter_get_arg_type(&var) !=
			    DBUS_TYPE_BOOLEAN)
				return FALSE;

			dbus_message_iter_get_basic(&var, abs_vol_supported);
		} else {
			syslog(LOG_WARNING, "%s not supported, ignoring", key);
		}
		dbus_message_iter_next(&dict);
	}

	if (remote_name == NULL || address == NULL)
		return FALSE;

	*name = remote_name;
	*addr = address;
	return TRUE;
}

static DBusHandlerResult
handle_bt_media_callback(DBusConnection *conn, DBusMessage *message, void *arg)
{
	int rc;
	const char *addr = NULL, *name = NULL;
	int a2dp_avail = 0, hfp_avail = 0;
	DBusError dbus_error;
	dbus_int32_t absolute_volume;
	dbus_int32_t hfp_cap;
	dbus_bool_t abs_vol_supported;
	struct cras_fl_a2dp_codec_config *codecs = NULL;

	syslog(LOG_DEBUG, "Bt Media callback message: %s %s %s",
	       dbus_message_get_path(message),
	       dbus_message_get_interface(message),
	       dbus_message_get_member(message));

	if (dbus_message_is_method_call(message, BT_MEDIA_CALLBACK_INTERFACE,
					"OnBluetoothAudioDeviceAdded")) {
		dbus_error_init(&dbus_error);
		if (!parse_bluetooth_audio_device_added(message, &addr, &name,
							&codecs, &hfp_cap,
							&abs_vol_supported)) {
			return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
		}

		syslog(LOG_DEBUG, "OnBluetoothAudioDeviceAdded %s %s", addr,
		       name);

		if (!active_fm) {
			syslog(LOG_WARNING, "Floss media object not ready");
			return DBUS_HANDLER_RESULT_HANDLED;
		}

		a2dp_avail = cras_floss_get_a2dp_enabled() && codecs != NULL;
		hfp_avail = cras_floss_get_hfp_enabled() && hfp_cap;

		if (!a2dp_avail & !hfp_avail)
			return DBUS_HANDLER_RESULT_HANDLED;

		if (!active_fm->bt_io_mgr) {
			active_fm->bt_io_mgr = bt_io_manager_create();
			if (!active_fm->bt_io_mgr)
				return DBUS_HANDLER_RESULT_HANDLED;
		}

		if (a2dp_avail) {
			syslog(LOG_DEBUG, "A2DP device added.");
			if (active_fm->a2dp) {
				syslog(LOG_WARNING,
				       "Multiple A2DP devices added, remove the older");
				bt_io_manager_remove_iodev(
					active_fm->bt_io_mgr,
					cras_floss_a2dp_get_iodev(
						active_fm->a2dp));
				cras_floss_a2dp_destroy(active_fm->a2dp);
			}
			active_fm->a2dp = cras_floss_a2dp_create(
				active_fm, addr, name, codecs);

			if (active_fm->a2dp) {
				cras_floss_a2dp_set_support_absolute_volume(
					active_fm->a2dp, abs_vol_supported);
				bt_io_manager_append_iodev(
					active_fm->bt_io_mgr,
					cras_floss_a2dp_get_iodev(
						active_fm->a2dp),
					CRAS_BT_FLAG_A2DP);
			} else {
				syslog(LOG_WARNING,
				       "Failed to create the cras_a2dp_manager");
			}
		}

		if (hfp_avail) {
			syslog(LOG_DEBUG, "HFP device added.");
			if (active_fm->hfp) {
				syslog(LOG_WARNING,
				       "Multiple HFP devices added, remove the older");
				bt_io_manager_remove_iodev(
					active_fm->bt_io_mgr,
					cras_floss_hfp_get_input_iodev(
						active_fm->hfp));
				bt_io_manager_remove_iodev(
					active_fm->bt_io_mgr,
					cras_floss_hfp_get_output_iodev(
						active_fm->hfp));
				cras_floss_hfp_destroy(active_fm->hfp);
			}
			active_fm->hfp =
				cras_floss_hfp_create(active_fm, addr, name);

			if (active_fm->hfp) {
				bt_io_manager_append_iodev(
					active_fm->bt_io_mgr,
					cras_floss_hfp_get_input_iodev(
						active_fm->hfp),
					CRAS_BT_FLAG_HFP);
				bt_io_manager_append_iodev(
					active_fm->bt_io_mgr,
					cras_floss_hfp_get_output_iodev(
						active_fm->hfp),
					CRAS_BT_FLAG_HFP);
			} else {
				syslog(LOG_WARNING,
				       "Failed to create the cras_hfp_manager");
			}
		}
		bt_io_manager_set_nodes_plugged(active_fm->bt_io_mgr, 1);

		return DBUS_HANDLER_RESULT_HANDLED;
	} else if (dbus_message_is_method_call(
			   message, BT_MEDIA_CALLBACK_INTERFACE,
			   "OnBluetoothAudioDeviceRemoved")) {
		rc = get_single_arg(message, DBUS_TYPE_STRING, &addr);
		if (rc) {
			syslog(LOG_ERR,
			       "Failed to get addr from OnBluetoothAudioDeviceRemoved");
			return rc;
		}

		syslog(LOG_DEBUG, "OnBluetoothAudioDeviceRemoved %s", addr);

		if (!active_fm) {
			syslog(LOG_ERR, "fl_media hasn't started or stopped");
			return DBUS_HANDLER_RESULT_HANDLED;
		}

		if (!active_fm->bt_io_mgr) {
			syslog(LOG_ERR, "No device has been added.");
			return DBUS_HANDLER_RESULT_HANDLED;
		}

		bt_io_manager_set_nodes_plugged(active_fm->bt_io_mgr, 0);
		if (active_fm && active_fm->a2dp) {
			floss_media_a2dp_suspend(active_fm);
		}
		if (active_fm && active_fm->hfp) {
			bt_io_manager_remove_iodev(
				active_fm->bt_io_mgr,
				cras_floss_hfp_get_input_iodev(active_fm->hfp));
			bt_io_manager_remove_iodev(
				active_fm->bt_io_mgr,
				cras_floss_hfp_get_output_iodev(
					active_fm->hfp));
			cras_floss_hfp_destroy(active_fm->hfp);
			active_fm->hfp = NULL;
		}

		return DBUS_HANDLER_RESULT_HANDLED;
	} else if (dbus_message_is_method_call(
			   message, BT_MEDIA_CALLBACK_INTERFACE,
			   "OnAbsoluteVolumeSupportedChanged")) {
		rc = get_single_arg(message, DBUS_TYPE_BOOLEAN,
				    &abs_vol_supported);
		if (rc) {
			syslog(LOG_ERR,
			       "Failed to get support from OnAvrcpConnected");
			return rc;
		}
		syslog(LOG_DEBUG, "OnAbsoluteVolumeSupportedChanged %d",
		       abs_vol_supported);
		if (active_fm && active_fm->a2dp)
			cras_floss_a2dp_set_support_absolute_volume(
				active_fm->a2dp, abs_vol_supported);

		return DBUS_HANDLER_RESULT_HANDLED;
	} else if (dbus_message_is_method_call(message,
					       BT_MEDIA_CALLBACK_INTERFACE,
					       "OnAbsoluteVolumeChanged")) {
		rc = get_single_arg(message, DBUS_TYPE_INT32, &absolute_volume);
		if (rc) {
			syslog(LOG_ERR,
			       "Failed to get volume from OnAbsoluteVolumeChanged");
			return rc;
		}

		if (absolute_volume < 0 || !active_fm || !active_fm->a2dp) {
			syslog(LOG_WARNING,
			       "Invalid volume or non-active a2dp device. Skip the volume update");
			return DBUS_HANDLER_RESULT_HANDLED;
		}
		syslog(LOG_DEBUG, "OnAbsoluteVolumeChanged %d",
		       absolute_volume);

		cras_floss_a2dp_update_volume(active_fm->a2dp, absolute_volume);

		return DBUS_HANDLER_RESULT_HANDLED;
	}
	return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
}

/* When we're notified about Floss media interface is ready. */
int floss_media_start(DBusConnection *conn, unsigned int hci)
{
	static const DBusObjectPathVTable control_vtable = {
		.message_function = handle_bt_media_callback,
	};
	DBusError dbus_error;

	// Register the callbacks to dbus daemon.
	dbus_error_init(&dbus_error);
	if (!dbus_connection_register_object_path(
		    conn, CRAS_BT_MEDIA_OBJECT_PATH, &control_vtable,
		    &dbus_error)) {
		syslog(LOG_ERR, "Couldn't register CRAS control: %s: %s",
		       CRAS_BT_MEDIA_OBJECT_PATH, dbus_error.message);
		dbus_error_free(&dbus_error);
		return -1;
	}

	/* Try to be cautious if Floss media gets the state wrong. */
	if (active_fm) {
		syslog(LOG_WARNING,
		       "Floss media %s already started, overriding by hci %u",
		       active_fm->obj_path, hci);
		free(active_fm);
	}

	active_fm = fl_media_create(hci);
	if (active_fm == NULL)
		return -ENOMEM;
	active_fm->conn = conn;

	syslog(LOG_DEBUG, "floss_media_start");
	floss_media_register_callback(conn, active_fm);
	// TODO: Call config codec to Floss when we support more than just SBC.
	return 0;
}

int floss_media_stop(DBusConnection *conn)
{
	if (!dbus_connection_unregister_object_path(conn,
						    CRAS_BT_MEDIA_OBJECT_PATH))
		syslog(LOG_WARNING, "Couldn't unregister BT media obj path");

	/* Clean up iodev when BT forced to stop. */
	if (active_fm) {
		if (active_fm->bt_io_mgr) {
			cras_bt_policy_remove_io_manager(active_fm->bt_io_mgr);
			bt_io_manager_destroy(active_fm->bt_io_mgr);
		}
		if (active_fm->a2dp)
			cras_floss_a2dp_destroy(active_fm->a2dp);
		if (active_fm->hfp)
			cras_floss_hfp_destroy(active_fm->hfp);
		free(active_fm);
		active_fm = NULL;
	}
	return 0;
}
