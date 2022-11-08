/* Copyright 2013 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "cras/src/server/cras_dbus_control.h"

#include <dbus/dbus.h>
#include <errno.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <syslog.h>

#include "cras/src/common/cras_dbus_bindings.h"  // Generated from Makefile
#include "cras/src/server/audio_thread.h"
#include "cras/src/server/cras_bt_player.h"
#include "cras/src/server/cras_dbus.h"
#include "cras/src/server/cras_dbus_util.h"
#include "cras/src/server/cras_fl_manager.h"
#include "cras/src/server/cras_hfp_ag_profile.h"
#include "cras/src/server/cras_iodev.h"
#include "cras/src/server/cras_iodev_list.h"
#include "cras/src/server/cras_main_thread_log.h"
#include "cras/src/server/cras_observer.h"
#include "cras/src/server/cras_rtc.h"
#include "cras/src/server/cras_system_state.h"
#include "cras/src/server/cras_utf8.h"
#include "cras/src/server/softvol_curve.h"
#include "cras_util.h"
#include "third_party/utlist/utlist.h"

struct cras_dbus_control {
  DBusConnection* conn;
  struct cras_observer_client* observer;
};
static struct cras_dbus_control dbus_control;

static bool get_string_metadata(DBusMessageIter* iter, const char** dst) {
  if (dbus_message_iter_get_arg_type(iter) != DBUS_TYPE_STRING) {
    return FALSE;
  }

  dbus_message_iter_get_basic(iter, dst);
  return TRUE;
}

static bool get_int64_metadata(DBusMessageIter* iter, dbus_int64_t* dst) {
  if (dbus_message_iter_get_arg_type(iter) != DBUS_TYPE_INT64) {
    return FALSE;
  }

  dbus_message_iter_get_basic(iter, dst);
  return TRUE;
}

static bool get_metadata(DBusMessage* message,
                         const char** title,
                         const char** artist,
                         const char** album,
                         dbus_int64_t* length) {
  DBusError dbus_error;
  DBusMessageIter iter, dict;

  dbus_error_init(&dbus_error);
  dbus_message_iter_init(message, &iter);

  if (dbus_message_iter_get_arg_type(&iter) != DBUS_TYPE_ARRAY) {
    return FALSE;
  }

  dbus_message_iter_recurse(&iter, &dict);

  while (dbus_message_iter_get_arg_type(&dict) != DBUS_TYPE_INVALID) {
    DBusMessageIter entry, var;
    const char* key;

    if (dbus_message_iter_get_arg_type(&dict) != DBUS_TYPE_DICT_ENTRY) {
      return FALSE;
    }

    dbus_message_iter_recurse(&dict, &entry);
    if (dbus_message_iter_get_arg_type(&entry) != DBUS_TYPE_STRING) {
      return FALSE;
    }

    dbus_message_iter_get_basic(&entry, &key);
    dbus_message_iter_next(&entry);

    if (dbus_message_iter_get_arg_type(&entry) != DBUS_TYPE_VARIANT) {
      return FALSE;
    }

    dbus_message_iter_recurse(&entry, &var);
    if (strcasecmp(key, "title") == 0) {
      if (!get_string_metadata(&var, title)) {
        return FALSE;
      }
    } else if (strcasecmp(key, "artist") == 0) {
      if (!get_string_metadata(&var, artist)) {
        return FALSE;
      }
    } else if (strcasecmp(key, "album") == 0) {
      if (!get_string_metadata(&var, album)) {
        return FALSE;
      }
    } else if (strcasecmp(key, "length") == 0) {
      if (!get_int64_metadata(&var, length)) {
        return FALSE;
      }
    } else {
      syslog(LOG_WARNING, "%s not supported, ignoring", key);
    }

    dbus_message_iter_next(&dict);
  }

  return TRUE;
}

// Helper to send an empty reply.
static DBusHandlerResult send_empty_reply(DBusConnection* conn,
                                          DBusMessage* message) {
  DBusMessage* reply;
  dbus_uint32_t serial = 0;
  DBusHandlerResult ret = DBUS_HANDLER_RESULT_HANDLED;

  reply = dbus_message_new_method_return(message);
  if (!reply) {
    return DBUS_HANDLER_RESULT_NEED_MEMORY;
  }

  if (!dbus_connection_send(conn, reply, &serial)) {
    ret = DBUS_HANDLER_RESULT_NEED_MEMORY;
  }

  dbus_message_unref(reply);
  return ret;
}

// Helper to send an int32 reply.
static DBusHandlerResult send_int32_reply(DBusConnection* conn,
                                          DBusMessage* message,
                                          dbus_int32_t value) {
  DBusMessage* reply;
  dbus_uint32_t serial = 0;
  DBusHandlerResult ret = DBUS_HANDLER_RESULT_HANDLED;

  reply = dbus_message_new_method_return(message);
  if (!reply) {
    return DBUS_HANDLER_RESULT_NEED_MEMORY;
  }

  if (!dbus_message_append_args(reply, DBUS_TYPE_INT32, &value,
                                DBUS_TYPE_INVALID)) {
    ret = DBUS_HANDLER_RESULT_NEED_MEMORY;
    goto unref_reply;
  }
  if (!dbus_connection_send(conn, reply, &serial)) {
    ret = DBUS_HANDLER_RESULT_NEED_MEMORY;
  }

unref_reply:
  dbus_message_unref(reply);
  return ret;
}

void cras_dbus_notify_rtc_active(bool active) {
  DBusMessage* msg;
  int active_val = !!active;

  if (!dbus_control.conn) {
    syslog(LOG_WARNING, "%s: cras dbus connection not ready yet.", __func__);
    return;
  }

  msg =
      dbus_message_new_method_call("org.chromium.ResourceManager",   // name
                                   "/org/chromium/ResourceManager",  // path
                                   "org.chromium.ResourceManager",  // interface
                                   "SetRTCAudioActive");            // method
  if (!msg) {
    syslog(LOG_WARNING, "%s: Unable to create dbus message.", __func__);
    return;
  }

  if (!dbus_message_append_args(msg, DBUS_TYPE_BYTE, &active_val,
                                DBUS_TYPE_INVALID)) {
    syslog(LOG_WARNING, "%s: Unable to append bool to dbus message.", __func__);
    return;
  }

  if (!dbus_connection_send(dbus_control.conn, msg, NULL)) {
    syslog(LOG_WARNING, "%s: Error sending dbus message.", __func__);
  }
}

// Helper to send an bool reply.
static DBusHandlerResult send_bool_reply(DBusConnection* conn,
                                         DBusMessage* message,
                                         dbus_bool_t value) {
  DBusMessage* reply;
  dbus_uint32_t serial = 0;
  DBusHandlerResult ret = DBUS_HANDLER_RESULT_HANDLED;

  reply = dbus_message_new_method_return(message);
  if (!reply) {
    return DBUS_HANDLER_RESULT_NEED_MEMORY;
  }

  if (!dbus_message_append_args(reply, DBUS_TYPE_BOOLEAN, &value,
                                DBUS_TYPE_INVALID)) {
    ret = DBUS_HANDLER_RESULT_NEED_MEMORY;
    goto unref_reply;
  }
  if (!dbus_connection_send(conn, reply, &serial)) {
    ret = DBUS_HANDLER_RESULT_NEED_MEMORY;
  }

unref_reply:
  dbus_message_unref(reply);
  return ret;
}

// Handlers for exported DBus method calls.
static DBusHandlerResult handle_set_output_volume(DBusConnection* conn,
                                                  DBusMessage* message,
                                                  void* arg) {
  int rc;
  dbus_int32_t new_vol;

  rc = get_single_arg(message, DBUS_TYPE_INT32, &new_vol);
  if (rc) {
    return rc;
  }

  cras_system_set_volume(new_vol);

  send_empty_reply(conn, message);

  return DBUS_HANDLER_RESULT_HANDLED;
}

static DBusHandlerResult handle_set_output_node_volume(DBusConnection* conn,
                                                       DBusMessage* message,
                                                       void* arg) {
  dbus_int32_t new_vol;
  cras_node_id_t id;
  DBusError dbus_error;

  dbus_error_init(&dbus_error);

  if (!dbus_message_get_args(message, &dbus_error, DBUS_TYPE_UINT64, &id,
                             DBUS_TYPE_INT32, &new_vol, DBUS_TYPE_INVALID)) {
    syslog(LOG_WARNING, "Bad method received: %s", dbus_error.message);
    dbus_error_free(&dbus_error);
    return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
  }

  cras_iodev_list_set_node_attr(id, IONODE_ATTR_VOLUME, new_vol);

  send_empty_reply(conn, message);

  return DBUS_HANDLER_RESULT_HANDLED;
}

static DBusHandlerResult handle_set_display_rotation(DBusConnection* conn,
                                                     DBusMessage* message,
                                                     void* arg) {
  cras_node_id_t id;
  dbus_uint32_t rotation;
  DBusError dbus_error;

  dbus_error_init(&dbus_error);

  if (!dbus_message_get_args(message, &dbus_error, DBUS_TYPE_UINT64, &id,
                             DBUS_TYPE_UINT32, &rotation, DBUS_TYPE_INVALID)) {
    syslog(LOG_WARNING, "Bad method received: %s", dbus_error.message);
    dbus_error_free(&dbus_error);
    return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
  }

  if (!cras_validate_screen_rotation(rotation)) {
    syslog(LOG_WARNING, "Invalid display rotation received: %u", rotation);
  } else {
    cras_iodev_list_set_node_attr(id, IONODE_ATTR_DISPLAY_ROTATION, rotation);
  }

  send_empty_reply(conn, message);

  return DBUS_HANDLER_RESULT_HANDLED;
}

static DBusHandlerResult handle_swap_left_right(DBusConnection* conn,
                                                DBusMessage* message,
                                                void* arg) {
  cras_node_id_t id;
  dbus_bool_t swap;
  DBusError dbus_error;

  dbus_error_init(&dbus_error);

  if (!dbus_message_get_args(message, &dbus_error, DBUS_TYPE_UINT64, &id,
                             DBUS_TYPE_BOOLEAN, &swap, DBUS_TYPE_INVALID)) {
    syslog(LOG_WARNING, "Bad method received: %s", dbus_error.message);
    dbus_error_free(&dbus_error);
    return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
  }

  cras_iodev_list_set_node_attr(id, IONODE_ATTR_SWAP_LEFT_RIGHT, swap);

  send_empty_reply(conn, message);

  return DBUS_HANDLER_RESULT_HANDLED;
}

static DBusHandlerResult handle_set_output_mute(DBusConnection* conn,
                                                DBusMessage* message,
                                                void* arg) {
  int rc;
  dbus_bool_t new_mute;

  rc = get_single_arg(message, DBUS_TYPE_BOOLEAN, &new_mute);
  if (rc) {
    return rc;
  }

  cras_system_set_mute(new_mute);

  send_empty_reply(conn, message);

  return DBUS_HANDLER_RESULT_HANDLED;
}

static DBusHandlerResult handle_set_output_user_mute(DBusConnection* conn,
                                                     DBusMessage* message,
                                                     void* arg) {
  int rc;
  dbus_bool_t new_mute;

  rc = get_single_arg(message, DBUS_TYPE_BOOLEAN, &new_mute);
  if (rc) {
    return rc;
  }

  cras_system_set_user_mute(new_mute);
  MAINLOG(main_log, MAIN_THREAD_SET_OUTPUT_USER_MUTE, new_mute, 0, 0);

  send_empty_reply(conn, message);

  return DBUS_HANDLER_RESULT_HANDLED;
}

static DBusHandlerResult handle_set_suspend_audio(DBusConnection* conn,
                                                  DBusMessage* message,
                                                  void* arg) {
  int rc;
  dbus_bool_t suspend;
  rc = get_single_arg(message, DBUS_TYPE_BOOLEAN, &suspend);
  if (rc) {
    return rc;
  }

  cras_system_set_suspended(suspend);

  send_empty_reply(conn, message);

  return DBUS_HANDLER_RESULT_HANDLED;
}

static DBusHandlerResult handle_set_input_node_gain(DBusConnection* conn,
                                                    DBusMessage* message,
                                                    void* arg) {
  dbus_int32_t new_gain;
  cras_node_id_t id;
  DBusError dbus_error;

  dbus_error_init(&dbus_error);

  if (!dbus_message_get_args(message, &dbus_error, DBUS_TYPE_UINT64, &id,
                             DBUS_TYPE_INT32, &new_gain, DBUS_TYPE_INVALID)) {
    syslog(LOG_WARNING, "Bad method received: %s", dbus_error.message);
    dbus_error_free(&dbus_error);
    return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
  }

  cras_iodev_list_set_node_attr(id, IONODE_ATTR_CAPTURE_GAIN, new_gain);

  send_empty_reply(conn, message);

  return DBUS_HANDLER_RESULT_HANDLED;
}

static DBusHandlerResult handle_set_input_mute(DBusConnection* conn,
                                               DBusMessage* message,
                                               void* arg) {
  int rc;
  dbus_bool_t new_mute;

  rc = get_single_arg(message, DBUS_TYPE_BOOLEAN, &new_mute);
  if (rc) {
    return rc;
  }

  cras_system_set_capture_mute(new_mute);

  send_empty_reply(conn, message);

  return DBUS_HANDLER_RESULT_HANDLED;
}

static DBusHandlerResult handle_get_volume_state(DBusConnection* conn,
                                                 DBusMessage* message,
                                                 void* arg) {
  DBusMessage* reply;
  dbus_uint32_t serial = 0;
  dbus_int32_t volume;
  dbus_bool_t system_muted;
  dbus_bool_t user_muted;
  dbus_bool_t capture_muted;

  reply = dbus_message_new_method_return(message);

  volume = cras_system_get_volume();
  system_muted = cras_system_get_system_mute();
  user_muted = cras_system_get_user_mute();
  capture_muted = cras_system_get_capture_mute();

  dbus_message_append_args(reply, DBUS_TYPE_INT32, &volume, DBUS_TYPE_BOOLEAN,
                           &system_muted, DBUS_TYPE_BOOLEAN, &capture_muted,
                           DBUS_TYPE_BOOLEAN, &user_muted, DBUS_TYPE_INVALID);

  dbus_connection_send(conn, reply, &serial);

  dbus_message_unref(reply);

  return DBUS_HANDLER_RESULT_HANDLED;
}

static DBusHandlerResult handle_get_default_output_buffer_size(
    DBusConnection* conn,
    DBusMessage* message,
    void* arg) {
  dbus_int32_t buffer_size = cras_system_get_default_output_buffer_size();

  send_int32_reply(conn, message, buffer_size);

  return DBUS_HANDLER_RESULT_HANDLED;
}

/* Appends the information about a node to the dbus message. Returns
 * false if not enough memory. */
static dbus_bool_t append_node_dict(DBusMessageIter* iter,
                                    const struct cras_iodev_info* dev,
                                    const struct cras_ionode_info* node,
                                    const struct audio_debug_info* info,
                                    enum CRAS_STREAM_DIRECTION direction) {
  DBusMessageIter dict;
  dbus_bool_t is_input;
  dbus_uint64_t id;
  const char* dev_name = dev->name;
  dbus_uint64_t stable_dev_id = node->stable_id;
  dbus_uint32_t max_supported_channels = dev->max_supported_channels;
  dbus_uint32_t last_open_result = dev->last_open_result;
  const char* node_type = node->type;
  const char* node_name = node->name;
  dbus_bool_t active;
  dbus_uint64_t plugged_time =
      node->plugged_time.tv_sec * 1000000ULL + node->plugged_time.tv_usec;
  dbus_uint64_t node_volume = node->volume;
  dbus_int64_t node_capture_gain = node->capture_gain;
  char *models, *empty_models = "";
  dbus_uint32_t node_audio_effect = node->audio_effect;
  dbus_int32_t node_number_of_volume_steps = node->number_of_volume_steps;

  is_input = (direction == CRAS_STREAM_INPUT);
  id = node->iodev_idx;
  id = (id << 32) | node->ionode_idx;
  active = !!node->active;

  uint32_t num_underruns = 0;
  uint32_t num_severe_underruns = 0;
  if (active && info) {
    for (int i = 0; i < info->num_devs; i++) {
      if (info->devs[i].dev_idx == node->iodev_idx) {
        num_underruns = info->devs[i].num_underruns;
        num_severe_underruns = info->devs[i].num_severe_underruns;
        break;
      }
    }
  }

  // If dev_name is not utf8, libdbus may abort cras.
  if (!is_utf8_string(dev_name)) {
    syslog(LOG_WARNING, "Non-utf8 device name '%s' cannot be sent via dbus",
           dev_name);
    dev_name = "";
  }

  if (!dbus_message_iter_open_container(iter, DBUS_TYPE_ARRAY, "{sv}", &dict)) {
    return FALSE;
  }
  if (!append_key_value(&dict, "IsInput", DBUS_TYPE_BOOLEAN,
                        DBUS_TYPE_BOOLEAN_AS_STRING, &is_input)) {
    return FALSE;
  }
  if (!append_key_value(&dict, "Id", DBUS_TYPE_UINT64,
                        DBUS_TYPE_UINT64_AS_STRING, &id)) {
    return FALSE;
  }
  if (!append_key_value(&dict, "DeviceName", DBUS_TYPE_STRING,
                        DBUS_TYPE_STRING_AS_STRING, &dev_name)) {
    return FALSE;
  }
  /*
   * If stable id migration is needed, use key 'StableDeviceIdNew'
   * together with 'StableDeviceId'.
   */
  if (!append_key_value(&dict, "StableDeviceId", DBUS_TYPE_UINT64,
                        DBUS_TYPE_UINT64_AS_STRING, &stable_dev_id)) {
    return FALSE;
  }
  if (!append_key_value(&dict, "MaxSupportedChannels", DBUS_TYPE_UINT32,
                        DBUS_TYPE_UINT32_AS_STRING, &max_supported_channels)) {
    return FALSE;
  }
  if (!append_key_value(&dict, "DeviceLastOpenResult", DBUS_TYPE_UINT32,
                        DBUS_TYPE_UINT32_AS_STRING, &last_open_result)) {
    return FALSE;
  }
  if (!append_key_value(&dict, "Type", DBUS_TYPE_STRING,
                        DBUS_TYPE_STRING_AS_STRING, &node_type)) {
    return FALSE;
  }
  if (!append_key_value(&dict, "Name", DBUS_TYPE_STRING,
                        DBUS_TYPE_STRING_AS_STRING, &node_name)) {
    return FALSE;
  }
  if (!append_key_value(&dict, "Active", DBUS_TYPE_BOOLEAN,
                        DBUS_TYPE_BOOLEAN_AS_STRING, &active)) {
    return FALSE;
  }
  if (!append_key_value(&dict, "PluggedTime", DBUS_TYPE_UINT64,
                        DBUS_TYPE_UINT64_AS_STRING, &plugged_time)) {
    return FALSE;
  }
  if (!append_key_value(&dict, "NodeVolume", DBUS_TYPE_UINT64,
                        DBUS_TYPE_UINT64_AS_STRING, &node_volume)) {
    return FALSE;
  }
  if (!append_key_value(&dict, "NodeCaptureGain", DBUS_TYPE_INT64,
                        DBUS_TYPE_INT64_AS_STRING, &node_capture_gain)) {
    return FALSE;
  }
  if (!append_key_value(&dict, "AudioEffect", DBUS_TYPE_UINT32,
                        DBUS_TYPE_UINT32_AS_STRING, &node_audio_effect)) {
    return FALSE;
  }
  if (!append_key_value(&dict, "NumberOfVolumeSteps", DBUS_TYPE_INT32,
                        DBUS_TYPE_INT32_AS_STRING,
                        &node_number_of_volume_steps)) {
    return FALSE;
  }

  if (is_input) {
    uint32_t input_node_gain;

    input_node_gain = convert_input_node_gain_from_dBFS(
        convert_dBFS_from_softvol_scaler(node->ui_gain_scaler),
        cras_iodev_is_node_type_internal_mic(node_type));
    if (!append_key_value(&dict, "InputNodeGain", DBUS_TYPE_UINT32,
                          DBUS_TYPE_UINT32_AS_STRING, &input_node_gain)) {
      return FALSE;
    }
  }

  models = cras_iodev_list_get_hotword_models(id);
  if (!append_key_value(&dict, "HotwordModels", DBUS_TYPE_STRING,
                        DBUS_TYPE_STRING_AS_STRING,
                        models ? &models : &empty_models)) {
    free(models);
    return FALSE;
  }
  free(models);

  if (active && info) {
    if (!append_key_value(&dict, "NumberOfUnderruns", DBUS_TYPE_UINT32,
                          DBUS_TYPE_UINT32_AS_STRING, &num_underruns)) {
      return FALSE;
    }
    if (!append_key_value(&dict, "NumberOfSevereUnderruns", DBUS_TYPE_UINT32,
                          DBUS_TYPE_UINT32_AS_STRING, &num_severe_underruns)) {
      return FALSE;
    }
  }

  if (!dbus_message_iter_close_container(iter, &dict)) {
    return FALSE;
  }

  return TRUE;
}

enum DUMP_DEBUG_INFO {
  DISABLED,
  ENABLED,
};

/* Appends the information about all nodes in a given direction. Returns false
 * if not enough memory. */
static dbus_bool_t append_nodes(enum CRAS_STREAM_DIRECTION direction,
                                const enum DUMP_DEBUG_INFO debug_info,
                                DBusMessageIter* array) {
  const struct cras_iodev_info* devs;
  const struct cras_ionode_info* nodes;
  struct audio_debug_info info = {};
  int ndevs, nnodes;
  int i, j;

  if (direction == CRAS_STREAM_OUTPUT) {
    ndevs = cras_system_state_get_output_devs(&devs);
    nnodes = cras_system_state_get_output_nodes(&nodes);
  } else {
    ndevs = cras_system_state_get_input_devs(&devs);
    nnodes = cras_system_state_get_input_nodes(&nodes);
  }

  if (debug_info == ENABLED) {
    audio_thread_dump_thread_info(cras_iodev_list_get_audio_thread(), &info);
  }

  for (i = 0; i < nnodes; i++) {
    // Don't reply unplugged nodes.
    if (!nodes[i].plugged) {
      continue;
    }
    // Find the device for this node.
    for (j = 0; j < ndevs; j++) {
      if (devs[j].idx == nodes[i].iodev_idx) {
        break;
      }
    }
    if (j == ndevs) {
      continue;
    }
    // Send information about this node.
    if (!append_node_dict(array, &devs[j], &nodes[i],
                          (debug_info == ENABLED ? &info : NULL), direction)) {
      return FALSE;
    }
  }

  return TRUE;
}

static DBusHandlerResult handle_get_nodes(DBusConnection* conn,
                                          DBusMessage* message,
                                          void* arg) {
  DBusMessage* reply;
  DBusMessageIter array;
  dbus_uint32_t serial = 0;

  reply = dbus_message_new_method_return(message);
  dbus_message_iter_init_append(reply, &array);
  if (!append_nodes(CRAS_STREAM_OUTPUT, DISABLED, &array)) {
    return DBUS_HANDLER_RESULT_NEED_MEMORY;
  }
  if (!append_nodes(CRAS_STREAM_INPUT, DISABLED, &array)) {
    return DBUS_HANDLER_RESULT_NEED_MEMORY;
  }
  dbus_connection_send(conn, reply, &serial);
  dbus_message_unref(reply);

  return DBUS_HANDLER_RESULT_HANDLED;
}

static DBusHandlerResult handle_get_node_infos(DBusConnection* conn,
                                               DBusMessage* message,
                                               void* arg) {
  DBusMessage* reply;
  DBusMessageIter array;
  DBusMessageIter dict;
  DBusHandlerResult rc = DBUS_HANDLER_RESULT_HANDLED;
  dbus_uint32_t serial = 0;

  reply = dbus_message_new_method_return(message);
  dbus_message_iter_init_append(reply, &array);
  if (!dbus_message_iter_open_container(&array, DBUS_TYPE_ARRAY, "a{sv}",
                                        &dict)) {
    rc = FALSE;
    goto error;
  }
  // Sets debug_info to ENABLED to get output underruns.
  if (!append_nodes(CRAS_STREAM_OUTPUT, ENABLED, &dict)) {
    rc = DBUS_HANDLER_RESULT_NEED_MEMORY;
    goto error;
  }
  if (!append_nodes(CRAS_STREAM_INPUT, DISABLED, &dict)) {
    rc = DBUS_HANDLER_RESULT_NEED_MEMORY;
    goto error;
  }
  if (!dbus_message_iter_close_container(&array, &dict)) {
    rc = FALSE;
    goto error;
  }
  dbus_connection_send(conn, reply, &serial);

error:
  dbus_message_unref(reply);
  return rc;
}

static DBusHandlerResult handle_get_system_aec_supported(DBusConnection* conn,
                                                         DBusMessage* message,
                                                         void* arg) {
  dbus_bool_t system_aec_supported = cras_system_get_aec_supported();

  send_bool_reply(conn, message, system_aec_supported);

  return DBUS_HANDLER_RESULT_HANDLED;
}

static DBusHandlerResult handle_get_system_aec_group_id(DBusConnection* conn,
                                                        DBusMessage* message,
                                                        void* arg) {
  dbus_int32_t system_aec_group_id = cras_system_get_aec_group_id();

  send_int32_reply(conn, message, system_aec_group_id);

  return DBUS_HANDLER_RESULT_HANDLED;
}

static DBusHandlerResult handle_get_system_ns_supported(DBusConnection* conn,
                                                        DBusMessage* message,
                                                        void* arg) {
  dbus_bool_t ns_supported = cras_system_get_ns_supported();

  send_bool_reply(conn, message, ns_supported);

  return DBUS_HANDLER_RESULT_HANDLED;
}

static DBusHandlerResult handle_get_system_agc_supported(DBusConnection* conn,
                                                         DBusMessage* message,
                                                         void* arg) {
  dbus_bool_t agc_supported = cras_system_get_agc_supported();

  send_bool_reply(conn, message, agc_supported);

  return DBUS_HANDLER_RESULT_HANDLED;
}

static DBusHandlerResult handle_get_deprioritize_bt_wbs_mic(
    DBusConnection* conn,
    DBusMessage* message,
    void* arg) {
  dbus_bool_t deprioritized = cras_system_get_deprioritize_bt_wbs_mic();

  send_bool_reply(conn, message, deprioritized);

  return DBUS_HANDLER_RESULT_HANDLED;
}

static DBusHandlerResult handle_get_rtc_running(DBusConnection* conn,
                                                DBusMessage* message,
                                                void* arg) {
  dbus_bool_t running = cras_rtc_is_running();

  send_bool_reply(conn, message, running);

  return DBUS_HANDLER_RESULT_HANDLED;
}

static DBusHandlerResult handle_set_active_node(
    DBusConnection* conn,
    DBusMessage* message,
    void* arg,
    enum CRAS_STREAM_DIRECTION direction) {
  int rc;
  cras_node_id_t id;

  rc = get_single_arg(message, DBUS_TYPE_UINT64, &id);
  if (rc) {
    return rc;
  }

  cras_iodev_list_select_node(direction, id);

  send_empty_reply(conn, message);

  return DBUS_HANDLER_RESULT_HANDLED;
}

static DBusHandlerResult handle_add_active_node(
    DBusConnection* conn,
    DBusMessage* message,
    void* arg,
    enum CRAS_STREAM_DIRECTION direction) {
  int rc;
  cras_node_id_t id;

  rc = get_single_arg(message, DBUS_TYPE_UINT64, &id);
  if (rc) {
    return rc;
  }

  cras_iodev_list_add_active_node(direction, id);

  send_empty_reply(conn, message);

  return DBUS_HANDLER_RESULT_HANDLED;
}

static DBusHandlerResult handle_remove_active_node(
    DBusConnection* conn,
    DBusMessage* message,
    void* arg,
    enum CRAS_STREAM_DIRECTION direction) {
  int rc;
  cras_node_id_t id;

  rc = get_single_arg(message, DBUS_TYPE_UINT64, &id);
  if (rc) {
    return rc;
  }

  cras_iodev_list_rm_active_node(direction, id);

  send_empty_reply(conn, message);

  return DBUS_HANDLER_RESULT_HANDLED;
}

static DBusHandlerResult handle_set_fix_a2dp_packet_size(DBusConnection* conn,
                                                         DBusMessage* message,
                                                         void* arg) {
  int rc;
  dbus_bool_t enabled = FALSE;

  rc = get_single_arg(message, DBUS_TYPE_BOOLEAN, &enabled);
  if (rc) {
    return rc;
  }

  cras_system_set_bt_fix_a2dp_packet_size_enabled(enabled);

  send_empty_reply(conn, message);

  return DBUS_HANDLER_RESULT_HANDLED;
}

static DBusHandlerResult handle_get_number_of_active_streams(
    DBusConnection* conn,
    DBusMessage* message,
    void* arg) {
  send_int32_reply(conn, message, cras_system_state_get_active_streams());
  return DBUS_HANDLER_RESULT_HANDLED;
}

static DBusHandlerResult handle_get_number_of_active_input_streams(
    DBusConnection* conn,
    DBusMessage* message,
    void* arg) {
  dbus_int32_t num = 0;
  unsigned i;

  for (i = 0; i < CRAS_NUM_DIRECTIONS; i++) {
    if (cras_stream_uses_input_hw(i)) {
      num += cras_system_state_get_active_streams_by_direction(i);
    }
  }
  send_int32_reply(conn, message, num);

  return DBUS_HANDLER_RESULT_HANDLED;
}

static DBusHandlerResult handle_get_number_of_active_output_streams(
    DBusConnection* conn,
    DBusMessage* message,
    void* arg) {
  dbus_int32_t num = 0;
  unsigned i;

  for (i = 0; i < CRAS_NUM_DIRECTIONS; i++) {
    if (cras_stream_uses_output_hw(i)) {
      num += cras_system_state_get_active_streams_by_direction(i);
    }
  }
  send_int32_reply(conn, message, num);

  return DBUS_HANDLER_RESULT_HANDLED;
}

static bool append_num_input_streams_with_permission(
    DBusMessage* message,
    uint32_t num_input_streams[CRAS_NUM_CLIENT_TYPE]) {
  DBusMessageIter array;
  DBusMessageIter dict;
  unsigned type;

  dbus_message_iter_init_append(message, &array);
  for (type = 0; type < CRAS_NUM_CLIENT_TYPE; ++type) {
    const char* client_type_str = cras_client_type_str(type);
    if (!is_utf8_string(client_type_str)) {
      syslog(LOG_WARNING,
             "Non-utf8 clinet_type_str '%s' cannot be sent "
             "via dbus",
             client_type_str);
      client_type_str = "";
    }

    if (!dbus_message_iter_open_container(&array, DBUS_TYPE_ARRAY, "{sv}",
                                          &dict)) {
      return false;
    }
    if (!append_key_value(&dict, "ClientType", DBUS_TYPE_STRING,
                          DBUS_TYPE_STRING_AS_STRING, &client_type_str)) {
      return false;
    }
    if (!append_key_value(&dict, "NumStreamsWithPermission", DBUS_TYPE_UINT32,
                          DBUS_TYPE_UINT32_AS_STRING,
                          &num_input_streams[type])) {
      return false;
    }
    if (!dbus_message_iter_close_container(&array, &dict)) {
      return false;
    }
  }
  return true;
}

static DBusHandlerResult handle_get_number_of_input_streams_with_permission(
    DBusConnection* conn,
    DBusMessage* message,
    void* arg) {
  DBusMessage* reply;
  dbus_uint32_t serial = 0;
  uint32_t num_input_streams[CRAS_NUM_CLIENT_TYPE] = {};

  reply = dbus_message_new_method_return(message);

  cras_system_state_get_input_streams_with_permission(num_input_streams);
  if (!append_num_input_streams_with_permission(reply, num_input_streams)) {
    goto error;
  }

  dbus_connection_send(conn, reply, &serial);
  dbus_message_unref(reply);
  return DBUS_HANDLER_RESULT_HANDLED;

error:
  dbus_message_unref(reply);
  return DBUS_HANDLER_RESULT_NEED_MEMORY;
}

static DBusHandlerResult handle_set_global_output_channel_remix(
    DBusConnection* conn,
    DBusMessage* message,
    void* arg) {
  dbus_int32_t num_channels;
  double* coeff_array;
  dbus_int32_t count;
  DBusError dbus_error;
  float* coefficient;
  int i;

  dbus_error_init(&dbus_error);

  if (!dbus_message_get_args(message, &dbus_error, DBUS_TYPE_INT32,
                             &num_channels, DBUS_TYPE_ARRAY, DBUS_TYPE_DOUBLE,
                             &coeff_array, &count, DBUS_TYPE_INVALID)) {
    syslog(LOG_WARNING, "Set global output channel remix error: %s",
           dbus_error.message);
    dbus_error_free(&dbus_error);
    return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
  }
  if (num_channels <= 0 || num_channels > CRAS_CH_MAX) {
    syslog(LOG_WARNING,
           "Set global output channel remix error: Invalid argument, "
           "num_channels[%d]",
           num_channels);
    return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
  }

  if (num_channels * num_channels != count) {
    syslog(LOG_WARNING,
           "Set global output channel remix error: Invalid argument, "
           "coeff_array size[%d] != num_channels[%d]*num_channels[%d]",
           count, num_channels, num_channels);
    return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
  }

  coefficient = (float*)calloc(count, sizeof(*coefficient));
  if (!coefficient) {
    return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
  }

  for (i = 0; i < count; i++) {
    coefficient[i] = coeff_array[i];
  }

  audio_thread_config_global_remix(cras_iodev_list_get_audio_thread(),
                                   num_channels, coefficient);

  send_empty_reply(conn, message);
  free(coefficient);
  return DBUS_HANDLER_RESULT_HANDLED;
}

static DBusHandlerResult handle_set_hotword_model(DBusConnection* conn,
                                                  DBusMessage* message,
                                                  void* arg) {
  cras_node_id_t id;
  const char* model_name;
  DBusError dbus_error;
  dbus_int32_t ret;

  dbus_error_init(&dbus_error);

  if (!dbus_message_get_args(message, &dbus_error, DBUS_TYPE_UINT64, &id,
                             DBUS_TYPE_STRING, &model_name,
                             DBUS_TYPE_INVALID)) {
    syslog(LOG_WARNING, "Bad method received: %s", dbus_error.message);
    dbus_error_free(&dbus_error);
    return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
  }

  ret = cras_iodev_list_set_hotword_model(id, model_name);
  send_int32_reply(conn, message, ret);

  return DBUS_HANDLER_RESULT_HANDLED;
}

static DBusHandlerResult handle_is_audio_output_active(DBusConnection* conn,
                                                       DBusMessage* message,
                                                       void* arg) {
  dbus_int32_t active = cras_system_state_get_non_empty_status();

  send_int32_reply(conn, message, active);

  return DBUS_HANDLER_RESULT_HANDLED;
}

static DBusHandlerResult handle_set_floss_enabled(DBusConnection* conn,
                                                  DBusMessage* message,
                                                  void* arg) {
  int rc;
  dbus_bool_t enabled;

  rc = get_single_arg(message, DBUS_TYPE_BOOLEAN, &enabled);
  if (rc) {
    return rc;
  }

  cras_floss_set_enabled(enabled);

  send_empty_reply(conn, message);

  return DBUS_HANDLER_RESULT_HANDLED;
}

static DBusHandlerResult handle_set_wbs_enabled(DBusConnection* conn,
                                                DBusMessage* message,
                                                void* arg) {
  int rc;
  dbus_bool_t enabled;

  rc = get_single_arg(message, DBUS_TYPE_BOOLEAN, &enabled);
  if (rc) {
    return rc;
  }

  cras_system_set_bt_wbs_enabled(enabled);

  send_empty_reply(conn, message);

  return DBUS_HANDLER_RESULT_HANDLED;
}

static DBusHandlerResult handle_set_noise_cancellation_enabled(
    DBusConnection* conn,
    DBusMessage* message,
    void* arg) {
  int rc;
  dbus_bool_t enabled;

  rc = get_single_arg(message, DBUS_TYPE_BOOLEAN, &enabled);
  if (rc) {
    return rc;
  }

  cras_system_set_noise_cancellation_enabled(enabled);

  send_empty_reply(conn, message);

  return DBUS_HANDLER_RESULT_HANDLED;
}

static DBusHandlerResult handle_is_noise_cancellation_supported(
    DBusConnection* conn,
    DBusMessage* message,
    void* arg) {
  dbus_bool_t supported = cras_system_get_noise_cancellation_supported();

  send_bool_reply(conn, message, supported);

  return DBUS_HANDLER_RESULT_HANDLED;
}

static DBusHandlerResult handle_set_bypass_block_noise_cancellation(
    DBusConnection* conn,
    DBusMessage* message,
    void* arg) {
  int rc;
  dbus_bool_t bypass;

  rc = get_single_arg(message, DBUS_TYPE_BOOLEAN, &bypass);
  if (rc) {
    return rc;
  }

  cras_system_set_bypass_block_noise_cancellation(bypass);

  send_empty_reply(conn, message);

  return DBUS_HANDLER_RESULT_HANDLED;
}

static DBusHandlerResult handle_set_force_sr_bt_enabled(DBusConnection* conn,
                                                        DBusMessage* message,
                                                        void* arg) {
  int rc;
  dbus_bool_t enabled;

  rc = get_single_arg(message, DBUS_TYPE_BOOLEAN, &enabled);
  if (rc) {
    return rc;
  }

  cras_system_set_force_sr_bt_enabled(enabled);

  send_empty_reply(conn, message);

  return DBUS_HANDLER_RESULT_HANDLED;
}

static DBusHandlerResult handle_get_force_sr_bt_enabled(DBusConnection* conn,
                                                        DBusMessage* message,
                                                        void* arg) {
  dbus_bool_t enabled = cras_system_get_force_sr_bt_enabled();

  send_bool_reply(conn, message, enabled);

  return DBUS_HANDLER_RESULT_HANDLED;
}

static DBusHandlerResult handle_set_player_playback_status(DBusConnection* conn,
                                                           DBusMessage* message,
                                                           void* arg) {
  char* status;
  DBusError dbus_error;
  int rc;

  dbus_error_init(&dbus_error);

  rc = get_single_arg(message, DBUS_TYPE_STRING, &status);
  if (rc) {
    return rc;
  }

  rc = cras_bt_player_update_playback_status(conn, status);
  if (rc) {
    syslog(LOG_WARNING, "CRAS failed to update BT Player Status: %d", rc);
  }

  send_empty_reply(conn, message);

  return DBUS_HANDLER_RESULT_HANDLED;
}

static DBusHandlerResult handle_set_player_identity(DBusConnection* conn,
                                                    DBusMessage* message,
                                                    void* arg) {
  char* identity;
  DBusError dbus_error;
  int rc;

  dbus_error_init(&dbus_error);

  rc = get_single_arg(message, DBUS_TYPE_STRING, &identity);
  if (rc) {
    return rc;
  }

  rc = cras_bt_player_update_identity(conn, identity);
  if (rc) {
    syslog(LOG_WARNING, "CRAS failed to update BT Player Identity: %d", rc);
  }

  send_empty_reply(conn, message);

  return DBUS_HANDLER_RESULT_HANDLED;
}

static DBusHandlerResult handle_set_player_position(DBusConnection* conn,
                                                    DBusMessage* message,
                                                    void* arg) {
  dbus_int64_t position;
  DBusError dbus_error;
  int rc;

  dbus_error_init(&dbus_error);

  rc = get_single_arg(message, DBUS_TYPE_INT64, &position);
  if (rc) {
    return rc;
  }

  rc = cras_bt_player_update_position(conn, position);
  if (rc) {
    syslog(LOG_WARNING, "CRAS failed to update BT Player Position: %d", rc);
  }

  send_empty_reply(conn, message);

  return DBUS_HANDLER_RESULT_HANDLED;
}

static DBusHandlerResult handle_set_player_metadata(DBusConnection* conn,
                                                    DBusMessage* message,
                                                    void* arg) {
  DBusError dbus_error;
  int rc;

  dbus_error_init(&dbus_error);
  const char *title = NULL, *artist = NULL, *album = NULL;
  dbus_int64_t length = 0;

  if (!get_metadata(message, &title, &artist, &album, &length)) {
    return -EINVAL;
  }

  rc = cras_bt_player_update_metadata(conn, title, artist, album, length);
  if (rc) {
    syslog(LOG_WARNING, "CRAS failed to update BT Metadata: %d", rc);
  }

  send_empty_reply(conn, message);

  return DBUS_HANDLER_RESULT_HANDLED;
}

static DBusHandlerResult handle_set_speak_on_mute_detection(
    DBusConnection* conn,
    DBusMessage* message,
    void* arg) {
  DBusError dbus_error;
  dbus_error_init(&dbus_error);

  dbus_bool_t enabled;
  int rc = get_single_arg(message, DBUS_TYPE_BOOLEAN, &enabled);
  if (rc) {
    return rc;
  }

  cras_system_state_set_speak_on_mute_detection(enabled);

  send_empty_reply(conn, message);
  return DBUS_HANDLER_RESULT_HANDLED;
}

static DBusHandlerResult handle_speak_on_mute_detection_enabled(
    DBusConnection* conn,
    DBusMessage* message,
    void* arg) {
  return send_bool_reply(
      conn, message, cras_system_state_get_speak_on_mute_detection_enabled());
}

static DBusHandlerResult handle_is_internal_card_detected(DBusConnection* conn,
                                                          DBusMessage* message,
                                                          void* arg) {
  dbus_bool_t internal_cards_detected =
      cras_system_state_internal_cards_detected();

  send_bool_reply(conn, message, internal_cards_detected);

  return DBUS_HANDLER_RESULT_HANDLED;
}

static inline DBusHandlerResult handle_get_number_of_non_chrome_output_streams(
    DBusConnection* conn,
    DBusMessage* message,
    void* arg) {
  return send_int32_reply(conn, message,
                          cras_system_state_num_non_chrome_output_streams());
}

static DBusHandlerResult handle_set_force_respect_ui_gains_enabled(
    DBusConnection* conn,
    DBusMessage* message,
    void* arg) {
  int rc;
  dbus_bool_t enabled;

  rc = get_single_arg(message, DBUS_TYPE_BOOLEAN, &enabled);
  if (rc) {
    return rc;
  }

  cras_system_set_force_respect_ui_gains_enabled(enabled);

  send_empty_reply(conn, message);

  return DBUS_HANDLER_RESULT_HANDLED;
}

// Handle incoming messages.
static DBusHandlerResult handle_control_message(DBusConnection* conn,
                                                DBusMessage* message,
                                                void* arg) {
  syslog(LOG_DEBUG, "Control message: %s %s %s", dbus_message_get_path(message),
         dbus_message_get_interface(message), dbus_message_get_member(message));

  if (dbus_message_is_method_call(message, DBUS_INTERFACE_INTROSPECTABLE,
                                  "Introspect")) {
    DBusMessage* reply;
    const char* xml = org_chromium_cras_Control_xml;

    reply = dbus_message_new_method_return(message);
    if (!reply) {
      return DBUS_HANDLER_RESULT_NEED_MEMORY;
    }
    if (!dbus_message_append_args(reply, DBUS_TYPE_STRING, &xml,
                                  DBUS_TYPE_INVALID)) {
      return DBUS_HANDLER_RESULT_NEED_MEMORY;
    }
    if (!dbus_connection_send(conn, reply, NULL)) {
      return DBUS_HANDLER_RESULT_NEED_MEMORY;
    }

    dbus_message_unref(reply);
    return DBUS_HANDLER_RESULT_HANDLED;

  } else if (dbus_message_is_method_call(message, CRAS_CONTROL_INTERFACE,
                                         "SetOutputVolume")) {
    return handle_set_output_volume(conn, message, arg);
  } else if (dbus_message_is_method_call(message, CRAS_CONTROL_INTERFACE,
                                         "SetOutputNodeVolume")) {
    return handle_set_output_node_volume(conn, message, arg);
  } else if (dbus_message_is_method_call(message, CRAS_CONTROL_INTERFACE,
                                         "SetDisplayRotation")) {
    return handle_set_display_rotation(conn, message, arg);
  } else if (dbus_message_is_method_call(message, CRAS_CONTROL_INTERFACE,
                                         "SwapLeftRight")) {
    return handle_swap_left_right(conn, message, arg);
  } else if (dbus_message_is_method_call(message, CRAS_CONTROL_INTERFACE,
                                         "SetOutputMute")) {
    return handle_set_output_mute(conn, message, arg);
  } else if (dbus_message_is_method_call(message, CRAS_CONTROL_INTERFACE,
                                         "SetOutputUserMute")) {
    return handle_set_output_user_mute(conn, message, arg);
  } else if (dbus_message_is_method_call(message, CRAS_CONTROL_INTERFACE,
                                         "SetSuspendAudio")) {
    return handle_set_suspend_audio(conn, message, arg);
  } else if (dbus_message_is_method_call(message, CRAS_CONTROL_INTERFACE,
                                         "SetInputNodeGain")) {
    return handle_set_input_node_gain(conn, message, arg);
  } else if (dbus_message_is_method_call(message, CRAS_CONTROL_INTERFACE,
                                         "SetInputMute")) {
    return handle_set_input_mute(conn, message, arg);
  } else if (dbus_message_is_method_call(message, CRAS_CONTROL_INTERFACE,
                                         "GetVolumeState")) {
    return handle_get_volume_state(conn, message, arg);
  } else if (dbus_message_is_method_call(message, CRAS_CONTROL_INTERFACE,
                                         "GetDefaultOutputBufferSize")) {
    return handle_get_default_output_buffer_size(conn, message, arg);
  } else if (dbus_message_is_method_call(message, CRAS_CONTROL_INTERFACE,
                                         "GetNodes")) {
    return handle_get_nodes(conn, message, arg);
  } else if (dbus_message_is_method_call(message, CRAS_CONTROL_INTERFACE,
                                         "GetNodeInfos")) {
    return handle_get_node_infos(conn, message, arg);
  } else if (dbus_message_is_method_call(message, CRAS_CONTROL_INTERFACE,
                                         "GetSystemAecSupported")) {
    return handle_get_system_aec_supported(conn, message, arg);
  } else if (dbus_message_is_method_call(message, CRAS_CONTROL_INTERFACE,
                                         "GetSystemAecGroupId")) {
    return handle_get_system_aec_group_id(conn, message, arg);
  } else if (dbus_message_is_method_call(message, CRAS_CONTROL_INTERFACE,
                                         "GetSystemNsSupported")) {
    return handle_get_system_ns_supported(conn, message, arg);
  } else if (dbus_message_is_method_call(message, CRAS_CONTROL_INTERFACE,
                                         "GetSystemAgcSupported")) {
    return handle_get_system_agc_supported(conn, message, arg);
  } else if (dbus_message_is_method_call(message, CRAS_CONTROL_INTERFACE,
                                         "GetDeprioritizeBtWbsMic")) {
    return handle_get_deprioritize_bt_wbs_mic(conn, message, arg);
  } else if (dbus_message_is_method_call(message, CRAS_CONTROL_INTERFACE,
                                         "GetRtcRunning")) {
    return handle_get_rtc_running(conn, message, arg);
  } else if (dbus_message_is_method_call(message, CRAS_CONTROL_INTERFACE,
                                         "SetActiveOutputNode")) {
    return handle_set_active_node(conn, message, arg, CRAS_STREAM_OUTPUT);
  } else if (dbus_message_is_method_call(message, CRAS_CONTROL_INTERFACE,
                                         "SetActiveInputNode")) {
    return handle_set_active_node(conn, message, arg, CRAS_STREAM_INPUT);
  } else if (dbus_message_is_method_call(message, CRAS_CONTROL_INTERFACE,
                                         "AddActiveInputNode")) {
    return handle_add_active_node(conn, message, arg, CRAS_STREAM_INPUT);
  } else if (dbus_message_is_method_call(message, CRAS_CONTROL_INTERFACE,
                                         "AddActiveOutputNode")) {
    return handle_add_active_node(conn, message, arg, CRAS_STREAM_OUTPUT);
  } else if (dbus_message_is_method_call(message, CRAS_CONTROL_INTERFACE,
                                         "RemoveActiveInputNode")) {
    return handle_remove_active_node(conn, message, arg, CRAS_STREAM_INPUT);
  } else if (dbus_message_is_method_call(message, CRAS_CONTROL_INTERFACE,
                                         "RemoveActiveOutputNode")) {
    return handle_remove_active_node(conn, message, arg, CRAS_STREAM_OUTPUT);
  } else if (dbus_message_is_method_call(message, CRAS_CONTROL_INTERFACE,
                                         "SetFixA2dpPacketSize")) {
    return handle_set_fix_a2dp_packet_size(conn, message, arg);
  } else if (dbus_message_is_method_call(message, CRAS_CONTROL_INTERFACE,
                                         "GetNumberOfActiveStreams")) {
    return handle_get_number_of_active_streams(conn, message, arg);
  } else if (dbus_message_is_method_call(message, CRAS_CONTROL_INTERFACE,
                                         "GetNumberOfActiveInputStreams")) {
    return handle_get_number_of_active_input_streams(conn, message, arg);
  } else if (dbus_message_is_method_call(
                 message, CRAS_CONTROL_INTERFACE,
                 "GetNumberOfInputStreamsWithPermission")) {
    return handle_get_number_of_input_streams_with_permission(conn, message,
                                                              arg);
  } else if (dbus_message_is_method_call(message, CRAS_CONTROL_INTERFACE,
                                         "GetNumberOfActiveOutputStreams")) {
    return handle_get_number_of_active_output_streams(conn, message, arg);
  } else if (dbus_message_is_method_call(message, CRAS_CONTROL_INTERFACE,
                                         "SetGlobalOutputChannelRemix")) {
    return handle_set_global_output_channel_remix(conn, message, arg);
  } else if (dbus_message_is_method_call(message, CRAS_CONTROL_INTERFACE,
                                         "SetHotwordModel")) {
    return handle_set_hotword_model(conn, message, arg);
  } else if (dbus_message_is_method_call(message, CRAS_CONTROL_INTERFACE,
                                         "IsAudioOutputActive")) {
    return handle_is_audio_output_active(conn, message, arg);
  } else if (dbus_message_is_method_call(message, CRAS_CONTROL_INTERFACE,
                                         "SetFlossEnabled")) {
    return handle_set_floss_enabled(conn, message, arg);
  } else if (dbus_message_is_method_call(message, CRAS_CONTROL_INTERFACE,
                                         "SetWbsEnabled")) {
    return handle_set_wbs_enabled(conn, message, arg);
  } else if (dbus_message_is_method_call(message, CRAS_CONTROL_INTERFACE,
                                         "SetNoiseCancellationEnabled")) {
    return handle_set_noise_cancellation_enabled(conn, message, arg);
  } else if (dbus_message_is_method_call(message, CRAS_CONTROL_INTERFACE,
                                         "IsNoiseCancellationSupported")) {
    return handle_is_noise_cancellation_supported(conn, message, arg);
  } else if (dbus_message_is_method_call(message, CRAS_CONTROL_INTERFACE,
                                         "SetBypassBlockNoiseCancellation")) {
    return handle_set_bypass_block_noise_cancellation(conn, message, arg);
  } else if (dbus_message_is_method_call(message, CRAS_CONTROL_INTERFACE,
                                         "SetForceSrBtEnabled")) {
    return handle_set_force_sr_bt_enabled(conn, message, arg);
  } else if (dbus_message_is_method_call(message, CRAS_CONTROL_INTERFACE,
                                         "GetForceSrBtEnabled")) {
    return handle_get_force_sr_bt_enabled(conn, message, arg);
  } else if (dbus_message_is_method_call(message, CRAS_CONTROL_INTERFACE,
                                         "SetPlayerPlaybackStatus")) {
    return handle_set_player_playback_status(conn, message, arg);
  } else if (dbus_message_is_method_call(message, CRAS_CONTROL_INTERFACE,
                                         "SetPlayerIdentity")) {
    return handle_set_player_identity(conn, message, arg);
  } else if (dbus_message_is_method_call(message, CRAS_CONTROL_INTERFACE,
                                         "SetPlayerPosition")) {
    return handle_set_player_position(conn, message, arg);
  } else if (dbus_message_is_method_call(message, CRAS_CONTROL_INTERFACE,
                                         "SetPlayerMetadata")) {
    return handle_set_player_metadata(conn, message, arg);
  } else if (dbus_message_is_method_call(message, CRAS_CONTROL_INTERFACE,
                                         "IsInternalCardDetected")) {
    return handle_is_internal_card_detected(conn, message, arg);
  } else if (dbus_message_is_method_call(message, CRAS_CONTROL_INTERFACE,
                                         "SetSpeakOnMuteDetection")) {
    return handle_set_speak_on_mute_detection(conn, message, arg);
  } else if (dbus_message_is_method_call(message, CRAS_CONTROL_INTERFACE,
                                         "SpeakOnMuteDetectionEnabled")) {
    return handle_speak_on_mute_detection_enabled(conn, message, arg);
  } else if (dbus_message_is_method_call(message, CRAS_CONTROL_INTERFACE,
                                         "GetNumberOfNonChromeOutputStreams")) {
    return handle_get_number_of_non_chrome_output_streams(conn, message, arg);
  } else if (dbus_message_is_method_call(message, CRAS_CONTROL_INTERFACE,
                                         "SetForceRespectUiGains")) {
    return handle_set_force_respect_ui_gains_enabled(conn, message, arg);
  }

  return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
}

// Creates a new DBus message, must be freed with dbus_message_unref.
static DBusMessage* create_dbus_message(const char* name) {
  DBusMessage* msg;
  msg = dbus_message_new_signal(CRAS_ROOT_OBJECT_PATH, CRAS_CONTROL_INTERFACE,
                                name);
  if (!msg) {
    syslog(LOG_WARNING, "Failed to create signal");
  }

  return msg;
}

// Handlers for system updates that generate DBus signals.

static void signal_output_volume_changed(void* context, int32_t volume) {
  struct cras_dbus_control* control = (struct cras_dbus_control*)context;
  dbus_uint32_t serial = 0;
  DBusMessage* msg;

  msg = create_dbus_message("OutputVolumeChanged");
  if (!msg) {
    return;
  }

  volume = cras_system_get_volume();
  dbus_message_append_args(msg, DBUS_TYPE_INT32, &volume, DBUS_TYPE_INVALID);
  dbus_connection_send(control->conn, msg, &serial);
  dbus_message_unref(msg);
}

static void signal_output_mute_changed(void* context,
                                       int muted,
                                       int user_muted,
                                       int mute_locked) {
  struct cras_dbus_control* control = (struct cras_dbus_control*)context;
  dbus_uint32_t serial = 0;
  DBusMessage* msg;

  msg = create_dbus_message("OutputMuteChanged");
  if (!msg) {
    return;
  }

  muted = cras_system_get_system_mute();
  user_muted = cras_system_get_user_mute();
  dbus_message_append_args(msg, DBUS_TYPE_BOOLEAN, &muted, DBUS_TYPE_BOOLEAN,
                           &user_muted, DBUS_TYPE_INVALID);
  dbus_connection_send(control->conn, msg, &serial);
  dbus_message_unref(msg);
}

static void signal_input_gain_changed(void* context, int32_t gain) {
  struct cras_dbus_control* control = (struct cras_dbus_control*)context;
  dbus_uint32_t serial = 0;
  DBusMessage* msg;

  msg = create_dbus_message("InputGainChanged");
  if (!msg) {
    return;
  }

  dbus_message_append_args(msg, DBUS_TYPE_INT32, &gain, DBUS_TYPE_INVALID);
  dbus_connection_send(control->conn, msg, &serial);
  dbus_message_unref(msg);
}

static void signal_input_mute_changed(void* context,
                                      int muted,
                                      int mute_locked) {
  struct cras_dbus_control* control = (struct cras_dbus_control*)context;
  dbus_uint32_t serial = 0;
  DBusMessage* msg;

  msg = create_dbus_message("InputMuteChanged");
  if (!msg) {
    return;
  }

  dbus_message_append_args(msg, DBUS_TYPE_BOOLEAN, &muted, DBUS_TYPE_INVALID);
  dbus_connection_send(control->conn, msg, &serial);
  dbus_message_unref(msg);
}

static void signal_nodes_changed(void* context) {
  struct cras_dbus_control* control = (struct cras_dbus_control*)context;
  dbus_uint32_t serial = 0;
  DBusMessage* msg;

  msg = create_dbus_message("NodesChanged");
  if (!msg) {
    return;
  }

  dbus_connection_send(control->conn, msg, &serial);
  dbus_message_unref(msg);
}

static void signal_active_node_changed(void* context,
                                       enum CRAS_STREAM_DIRECTION dir,
                                       cras_node_id_t node_id) {
  struct cras_dbus_control* control = (struct cras_dbus_control*)context;
  DBusMessage* msg;
  dbus_uint32_t serial = 0;

  msg = create_dbus_message((dir == CRAS_STREAM_OUTPUT)
                                ? "ActiveOutputNodeChanged"
                                : "ActiveInputNodeChanged");
  if (!msg) {
    return;
  }
  dbus_message_append_args(msg, DBUS_TYPE_UINT64, &node_id, DBUS_TYPE_INVALID);
  dbus_connection_send(control->conn, msg, &serial);
  dbus_message_unref(msg);
}

// Called by iodev_list when a node volume changes.
static void signal_output_node_volume_changed(void* context,
                                              cras_node_id_t node_id,
                                              int32_t volume) {
  struct cras_dbus_control* control = (struct cras_dbus_control*)context;
  dbus_uint32_t serial = 0;
  DBusMessage* msg;

  msg = create_dbus_message("OutputNodeVolumeChanged");
  if (!msg) {
    return;
  }

  dbus_message_append_args(msg, DBUS_TYPE_UINT64, &node_id, DBUS_TYPE_INT32,
                           &volume, DBUS_TYPE_INVALID);
  dbus_connection_send(control->conn, msg, &serial);
  dbus_message_unref(msg);
}

static void signal_input_node_gain_changed(void* context,
                                           cras_node_id_t node_id,
                                           int capture_gain) {
  struct cras_dbus_control* control = (struct cras_dbus_control*)context;
  dbus_uint32_t serial = 0;
  DBusMessage* msg;

  msg = create_dbus_message("InputNodeGainChanged");
  if (!msg) {
    return;
  }

  dbus_message_append_args(msg, DBUS_TYPE_UINT64, &node_id, DBUS_TYPE_INT32,
                           &capture_gain, DBUS_TYPE_INVALID);
  dbus_connection_send(control->conn, msg, &serial);
  dbus_message_unref(msg);
}

static void signal_node_left_right_swapped_changed(void* context,
                                                   cras_node_id_t node_id,
                                                   int swapped) {
  struct cras_dbus_control* control = (struct cras_dbus_control*)context;
  dbus_uint32_t serial = 0;
  DBusMessage* msg;

  msg = create_dbus_message("NodeLeftRightSwappedChanged");
  if (!msg) {
    return;
  }

  dbus_message_append_args(msg, DBUS_TYPE_UINT64, &node_id, DBUS_TYPE_BOOLEAN,
                           &swapped, DBUS_TYPE_INVALID);
  dbus_connection_send(control->conn, msg, &serial);
  dbus_message_unref(msg);
}

static void signal_number_of_active_streams_changed(
    void* context,
    enum CRAS_STREAM_DIRECTION dir,
    uint32_t num_active_streams) {
  struct cras_dbus_control* control = (struct cras_dbus_control*)context;
  dbus_uint32_t serial = 0;
  DBusMessage* msg;
  dbus_int32_t num;

  msg = create_dbus_message("NumberOfActiveStreamsChanged");
  if (!msg) {
    return;
  }

  num = cras_system_state_get_active_streams();
  dbus_message_append_args(msg, DBUS_TYPE_INT32, &num, DBUS_TYPE_INVALID);
  dbus_connection_send(control->conn, msg, &serial);
  dbus_message_unref(msg);
}

static void signal_number_of_non_chrome_output_stream_changed(
    void* context,
    uint32_t num_non_chrome_output_streams) {
  struct cras_dbus_control* control = (struct cras_dbus_control*)context;
  dbus_uint32_t serial = 0;
  dbus_int32_t num = num_non_chrome_output_streams;

  DBusMessage* msg =
      create_dbus_message("NumberOfNonChromeOutputStreamsChanged");
  if (!msg) {
    return;
  }

  dbus_message_append_args(msg, DBUS_TYPE_INT32, &num, DBUS_TYPE_INVALID);
  dbus_connection_send(control->conn, msg, &serial);
  dbus_message_unref(msg);
}

static void signal_number_of_input_streams_with_permission_changed(
    void* context,
    uint32_t num_input_streams[CRAS_NUM_CLIENT_TYPE]) {
  struct cras_dbus_control* control = (struct cras_dbus_control*)context;
  dbus_uint32_t serial = 0;
  DBusMessage* msg;

  msg = create_dbus_message("NumberOfInputStreamsWithPermissionChanged");
  if (!msg) {
    return;
  }

  if (!append_num_input_streams_with_permission(msg, num_input_streams)) {
    goto error;
  }

  dbus_connection_send(control->conn, msg, &serial);
error:
  dbus_message_unref(msg);
}

static void signal_hotword_triggered(void* context,
                                     int64_t tv_sec,
                                     int64_t tv_nsec) {
  struct cras_dbus_control* control = (struct cras_dbus_control*)context;
  dbus_uint32_t serial = 0;
  DBusMessage* msg;

  msg = create_dbus_message("HotwordTriggered");
  if (!msg) {
    return;
  }

  dbus_message_append_args(msg, DBUS_TYPE_INT64, &tv_sec, DBUS_TYPE_INT64,
                           &tv_nsec, DBUS_TYPE_INVALID);
  dbus_connection_send(control->conn, msg, &serial);
  dbus_message_unref(msg);
}

static void signal_audio_output_active_state_changed(void* context,
                                                     int non_empty) {
  struct cras_dbus_control* control = (struct cras_dbus_control*)context;

  dbus_uint32_t serial = 0;
  DBusMessage* msg;

  msg = create_dbus_message("AudioOutputActiveStateChanged");
  if (!msg) {
    return;
  }

  dbus_message_append_args(msg, DBUS_TYPE_BOOLEAN, &non_empty,
                           DBUS_TYPE_INVALID);

  dbus_connection_send(control->conn, msg, &serial);
  dbus_message_unref(msg);
}

static void signal_severe_underrun(void* context) {
  struct cras_dbus_control* control = (struct cras_dbus_control*)context;
  dbus_uint32_t serial = 0;

  DBusMessage* msg = create_dbus_message("SevereUnderrun");
  if (!msg) {
    return;
  }

  dbus_connection_send(control->conn, msg, &serial);
  dbus_message_unref(msg);
}

static void signal_underrun(void* context) {
  struct cras_dbus_control* control = (struct cras_dbus_control*)context;
  dbus_uint32_t serial = 0;

  DBusMessage* msg = create_dbus_message("Underrun");
  if (!msg) {
    return;
  }

  dbus_connection_send(control->conn, msg, &serial);
  dbus_message_unref(msg);
}

static bool fill_general_survey_dict(enum CRAS_STREAM_TYPE stream_type,
                                     enum CRAS_CLIENT_TYPE client_type,
                                     const char* node_type_pair,
                                     DBusMessageIter* dict)

{
  const char* stream_type_str = cras_stream_type_str(stream_type);
  if (!append_key_value(dict, "StreamType", DBUS_TYPE_STRING,
                        DBUS_TYPE_STRING_AS_STRING, &stream_type_str)) {
    return FALSE;
  }

  const char* client_type_str = cras_client_type_str(client_type);
  if (!append_key_value(dict, "ClientType", DBUS_TYPE_STRING,
                        DBUS_TYPE_STRING_AS_STRING, &client_type_str)) {
    return FALSE;
  }

  if (!append_key_value(dict, "NodeType", DBUS_TYPE_STRING,
                        DBUS_TYPE_STRING_AS_STRING, &node_type_pair)) {
    return FALSE;
  }

  return TRUE;
}

static void signal_general_survey(void* context,
                                  enum CRAS_STREAM_TYPE stream_type,
                                  enum CRAS_CLIENT_TYPE client_type,
                                  const char* node_type_pair) {
  struct cras_dbus_control* control = (struct cras_dbus_control*)context;
  dbus_uint32_t serial = 0;
  DBusMessage* msg;
  DBusMessageIter array;
  DBusMessageIter dict;

  msg = create_dbus_message("SurveyTrigger");
  if (!msg) {
    return;
  }

  dbus_message_iter_init_append(msg, &array);

  if (!dbus_message_iter_open_container(&array, DBUS_TYPE_ARRAY, "{sv}",
                                        &dict)) {
    goto error;
  }

  if (!fill_general_survey_dict(stream_type, client_type, node_type_pair,
                                &dict)) {
    goto error;
  }

  if (!dbus_message_iter_close_container(&array, &dict)) {
    goto error;
  }

  dbus_connection_send(control->conn, msg, &serial);

error:
  dbus_message_unref(msg);
}

static void signal_speak_on_mute_detected(void* context) {
  struct cras_dbus_control* control = (struct cras_dbus_control*)context;
  dbus_uint32_t serial = 0;

  DBusMessage* msg = create_dbus_message("SpeakOnMuteDetected");
  if (!msg) {
    return;
  }

  dbus_connection_send(control->conn, msg, &serial);
  dbus_message_unref(msg);
}

// Exported Interface

void cras_dbus_control_start(DBusConnection* conn) {
  static const DBusObjectPathVTable control_vtable = {
      .message_function = handle_control_message,
  };

  DBusError dbus_error;
  struct cras_observer_ops observer_ops;

  dbus_control.conn = conn;
  dbus_connection_ref(dbus_control.conn);

  if (!dbus_connection_register_object_path(conn, CRAS_ROOT_OBJECT_PATH,
                                            &control_vtable, &dbus_error)) {
    syslog(LOG_WARNING, "Couldn't register CRAS control: %s: %s",
           CRAS_ROOT_OBJECT_PATH, dbus_error.message);
    dbus_error_free(&dbus_error);
    return;
  }

  memset(&observer_ops, 0, sizeof(observer_ops));
  observer_ops.output_volume_changed = signal_output_volume_changed;
  observer_ops.output_mute_changed = signal_output_mute_changed;
  observer_ops.capture_gain_changed = signal_input_gain_changed;
  observer_ops.capture_mute_changed = signal_input_mute_changed;
  observer_ops.num_active_streams_changed =
      signal_number_of_active_streams_changed;
  observer_ops.num_non_chrome_output_streams_changed =
      signal_number_of_non_chrome_output_stream_changed;
  observer_ops.num_input_streams_with_permission_changed =
      signal_number_of_input_streams_with_permission_changed;
  observer_ops.nodes_changed = signal_nodes_changed;
  observer_ops.active_node_changed = signal_active_node_changed;
  observer_ops.input_node_gain_changed = signal_input_node_gain_changed;
  observer_ops.output_node_volume_changed = signal_output_node_volume_changed;
  observer_ops.node_left_right_swapped_changed =
      signal_node_left_right_swapped_changed;
  observer_ops.hotword_triggered = signal_hotword_triggered;
  observer_ops.non_empty_audio_state_changed =
      signal_audio_output_active_state_changed;
  observer_ops.severe_underrun = signal_severe_underrun;
  observer_ops.underrun = signal_underrun;
  observer_ops.general_survey = signal_general_survey;
  observer_ops.speak_on_mute_detected = signal_speak_on_mute_detected;

  dbus_control.observer = cras_observer_add(&observer_ops, &dbus_control);
}

void cras_dbus_control_stop() {
  if (!dbus_control.conn) {
    return;
  }

  dbus_connection_unregister_object_path(dbus_control.conn,
                                         CRAS_ROOT_OBJECT_PATH);

  dbus_connection_unref(dbus_control.conn);
  dbus_control.conn = NULL;
  cras_observer_remove(dbus_control.observer);
  dbus_control.observer = NULL;
}
