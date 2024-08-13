/* Copyright 2021 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "cras/src/server/cras_fl_media.h"

#include <dbus/dbus.h>
#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <syslog.h>
#include <time.h>

#include "cras/src/server/cras_a2dp_manager.h"
#include "cras/src/server/cras_bt_io.h"
#include "cras/src/server/cras_dbus_util.h"
#include "cras/src/server/cras_fl_media_adapter.h"
#include "cras/src/server/cras_hfp_manager.h"
#include "third_party/utlist/utlist.h"

#define BT_SERVICE_NAME "org.chromium.bluetooth"
// Object path is of the form BT_OBJECT_BASE + hci + BT_OBJECT_MEDIA
#define BT_OBJECT_BASE "/org/chromium/bluetooth/hci"
#define BT_OBJECT_MEDIA "/media"
#define BT_MEDIA_INTERFACE "org.chromium.bluetooth.BluetoothMedia"
#define BT_OBJECT_TELEPHONY "/telephony"
#define BT_TELEPHONY_INTERFACE "org.chromium.bluetooth.BluetoothTelephony"

#define BT_MEDIA_CALLBACK_INTERFACE \
  "org.chromium.bluetooth.BluetoothMediaCallback"

#define BT_TELEPHONY_CALLBACK_INTERFACE \
  "org.chromium.bluetooth.BluetoothTelephonyCallback"

#define CRAS_BT_MEDIA_OBJECT_PATH "/org/chromium/cras/bluetooth/media"

// TODO(jrwu): monitor stats for cases that take > 2s

#define POLL_AUDIO_STATUS_TIMEOUT_MS 5000

#define GET_A2DP_AUDIO_STARTED_RETRIES 1000
#define GET_A2DP_AUDIO_STARTED_SLEEP_US 5000

#define GET_HFP_AUDIO_STARTED_RETRIES 1000
#define GET_HFP_AUDIO_STARTED_SLEEP_US 5000

#define GET_LEA_AUDIO_STARTED_RETRIES 1000
#define GET_LEA_AUDIO_STARTED_SLEEP_US 5000

#define LEA_AUDIO_OP_RETRIES 1000
#define LEA_AUDIO_OP_US 5000

static struct fl_media* active_fm = NULL;

struct fl_media* floss_media_get_active_fm() {
  return active_fm;
}

unsigned int floss_media_get_active_hci() {
  if (active_fm == NULL) {
    syslog(LOG_WARNING, "Queried the active HCI device when there is none.");
    return 0;
  }
  return active_fm->hci;
}

int fl_media_init(int hci) {
  struct fl_media* fm = (struct fl_media*)calloc(1, sizeof(*fm));

  if (fm == NULL) {
    active_fm = NULL;
    return -ENOMEM;
  }
  fm->hci = hci;
  snprintf(fm->obj_path, BT_MEDIA_OBJECT_PATH_SIZE_MAX, "%s%d%s",
           BT_OBJECT_BASE, hci, BT_OBJECT_MEDIA);
  snprintf(fm->obj_telephony_path, BT_TELEPHONY_OBJECT_PATH_SIZE_MAX, "%s%d%s",
           BT_OBJECT_BASE, hci, BT_OBJECT_TELEPHONY);
  active_fm = fm;
  return 0;
}

static int poll_fd_for_events(unsigned int events, int fd, int timeout_ms) {
  int rc = 0;

  struct pollfd pfd = {
      .fd = fd,
      .events = events,
      .revents = 0,
  };
  rc = poll(&pfd, 1, timeout_ms);

  if (rc == 0) {
    syslog(LOG_WARNING, "%s: timed out polling the fd", __func__);
    return -ETIMEDOUT;
  }

  if (rc == -1) {
    syslog(LOG_WARNING, "%s: failed to poll the fd, rc = %d", __func__, errno);
    return -errno;
  }

  if (!(pfd.revents & events)) {
    syslog(LOG_WARNING, "%s: got unexpected events %d", __func__, pfd.revents);
    return -EINVAL;
  }

  return 0;
}

static bool dbus_int32_is_nonzero(int dbus_type, void* dbus_value_ptr) {
  if (dbus_type != DBUS_TYPE_INT32) {
    syslog(LOG_ERR,
           "Mismatched return type, "
           "expected type id: %d, received type id: %d",
           DBUS_TYPE_INT32, dbus_type);
    return false;
  }
  return *(dbus_int32_t*)dbus_value_ptr != 0;
}

static bool dbus_int32_as_group_stream_status_is_idle(int dbus_type,
                                                      void* dbus_value_ptr) {
  if (dbus_type != DBUS_TYPE_INT32) {
    syslog(LOG_ERR,
           "Mismatched return type, "
           "expected type id: %d, received type id: %d",
           DBUS_TYPE_INT32, dbus_type);
    return false;
  }
  return *(dbus_int32_t*)dbus_value_ptr == FL_LEA_GROUP_STREAM_STATUS_IDLE ||
         *(dbus_int32_t*)dbus_value_ptr ==
             FL_LEA_GROUP_STREAM_STATUS_CONFIGURED_AUTONOMOUS;
}

static bool dbus_int32_as_group_status_is_active(int dbus_type,
                                                 void* dbus_value_ptr) {
  if (dbus_type != DBUS_TYPE_INT32) {
    syslog(LOG_ERR,
           "Mismatched return type, "
           "expected type id: %d, received type id: %d",
           DBUS_TYPE_INT32, dbus_type);
    return false;
  }
  return *(dbus_int32_t*)dbus_value_ptr == FL_LEA_GROUP_ACTIVE;
}

static bool dbus_int32_as_group_status_is_inactive(int dbus_type,
                                                   void* dbus_value_ptr) {
  if (dbus_type != DBUS_TYPE_INT32) {
    syslog(LOG_ERR,
           "Mismatched return type, "
           "expected type id: %d, received type id: %d",
           DBUS_TYPE_INT32, dbus_type);
    return false;
  }
  return *(dbus_int32_t*)dbus_value_ptr == FL_LEA_GROUP_INACTIVE;
}

int floss_media_hfp_set_active_device(struct fl_media* fm, const char* addr) {
  RET_IF_HAVE_FUZZER(0);
  DBusMessage *method_call, *reply;
  DBusError dbus_error;

  syslog(LOG_DEBUG, "floss_media_set_hfp_active_device");

  method_call = dbus_message_new_method_call(
      BT_SERVICE_NAME, fm->obj_path, BT_MEDIA_INTERFACE, "SetHfpActiveDevice");
  if (!method_call) {
    return -ENOMEM;
  }

  if (!dbus_message_append_args(method_call, DBUS_TYPE_STRING, &addr,
                                DBUS_TYPE_INVALID)) {
    dbus_message_unref(method_call);
    return -ENOMEM;
  }

  dbus_error_init(&dbus_error);
  reply = dbus_connection_send_with_reply_and_block(
      active_fm->conn, method_call, DBUS_TIMEOUT_USE_DEFAULT, &dbus_error);
  if (!reply) {
    syslog(LOG_ERR, "Failed to send SetHfpActiveDevice: %s",
           dbus_error.message);
    dbus_error_free(&dbus_error);
    dbus_message_unref(method_call);
    return -EIO;
  }

  dbus_message_unref(method_call);

  if (dbus_message_get_type(reply) == DBUS_MESSAGE_TYPE_ERROR) {
    syslog(LOG_ERR, "SetHfpActiveDevice returned error: %s",
           dbus_message_get_error_name(reply));
    dbus_message_unref(reply);
    return -EIO;
  }

  dbus_message_unref(reply);

  return 0;
}

int floss_media_hfp_start_sco_call(struct fl_media* fm,
                                   const char* addr,
                                   bool enable_offload,
                                   int disabled_codecs) {
  RET_IF_HAVE_FUZZER(0);

  int rc = 0;

  syslog(LOG_DEBUG, "%s: %s", __func__, addr);

  if (!fm) {
    syslog(LOG_WARNING, "%s: Floss media not started", __func__);
    return -EINVAL;
  }

  int pipefd[2];
  if (pipe(pipefd) == -1) {
    syslog(LOG_WARNING, "%s: failed to create pipe, rc = %d", __func__, errno);
    return -errno;
  }

  dbus_bool_t dbus_enable_offload = enable_offload;
  dbus_int32_t dbus_disabled_codecs = disabled_codecs;

  DBusMessage* start_sco_call;
  rc = create_dbus_method_call(
      /* method_call= */ &start_sco_call,
      /* dest= */ BT_SERVICE_NAME,
      /* path= */ fm->obj_path,
      /* iface= */ BT_MEDIA_INTERFACE,
      /* method_name= */ "StartScoCall",
      /* num_args= */ 4,
      /* arg1= */ DBUS_TYPE_STRING, &addr,
      /* arg2= */ DBUS_TYPE_BOOLEAN, &dbus_enable_offload,
      /* arg3= */ DBUS_TYPE_INT32, &dbus_disabled_codecs,
      /* arg4= */ DBUS_TYPE_UNIX_FD, &pipefd[1]);

  if (rc < 0) {
    goto leave;
  }

  dbus_bool_t response = FALSE;
  rc = call_method_and_parse_reply(
      /* conn= */ fm->conn,
      /* method_call= */ start_sco_call,
      /* dbus_ret_type= */ DBUS_TYPE_BOOLEAN,
      /* dbus_ret_value_ptr= */ &response,
      /* log_on_error= */ true);

  dbus_message_unref(start_sco_call);

  if (rc < 0) {
    goto leave;
  }

  if (response == FALSE) {
    syslog(LOG_WARNING, "%s: request was rejected", __func__);
    rc = -EPERM;
    goto leave;
  }

  rc = poll_fd_for_events(
      /* events= */ POLLIN,
      /* fd= */ pipefd[0],
      /* timeout_ms= */ POLL_AUDIO_STATUS_TIMEOUT_MS);

  if (rc < 0) {
    syslog(LOG_WARNING, "%s: failed to wait for results", __func__);
    goto leave;
  }

  uint8_t codec_id = 0;
  ssize_t nread = read(pipefd[0], &codec_id, sizeof(codec_id));
  if (nread != sizeof(codec_id)) {
    syslog(LOG_WARNING, "%s: failed to read from pipe, rc = %d", __func__,
           errno);
    rc = -errno;
    goto leave;
  }

  rc = codec_id;

leave:
  close(pipefd[0]);
  close(pipefd[1]);

  return rc;
}

int floss_media_hfp_stop_sco_call(struct fl_media* fm, const char* addr) {
  RET_IF_HAVE_FUZZER(0);

  int rc = 0;

  syslog(LOG_DEBUG, "%s: %s", __func__, addr);

  if (!fm) {
    syslog(LOG_WARNING, "%s: Floss media not started", __func__);
    return -EINVAL;
  }

  int pipefd[2];
  if (pipe(pipefd) == -1) {
    syslog(LOG_WARNING, "%s: failed to create pipe, rc = %d", __func__, errno);
    return -errno;
  }

  DBusMessage* stop_sco_call;
  rc = create_dbus_method_call(
      /* method_call= */ &stop_sco_call,
      /* dest= */ BT_SERVICE_NAME,
      /* path= */ fm->obj_path,
      /* iface= */ BT_MEDIA_INTERFACE,
      /* method_name= */ "StopScoCall",
      /* num_args= */ 2,
      /* arg1= */ DBUS_TYPE_STRING, &addr,
      /* arg2= */ DBUS_TYPE_UNIX_FD, &pipefd[1]);

  if (rc < 0) {
    goto leave;
  }

  rc = call_method_and_parse_reply(
      /* conn= */ fm->conn,
      /* method_call= */ stop_sco_call,
      /* dbus_ret_type= */ DBUS_TYPE_INVALID,
      /* dbus_ret_value_ptr= */ NULL,
      /* log_on_error= */ true);

  dbus_message_unref(stop_sco_call);

  if (rc < 0) {
    goto leave;
  }

  rc = poll_fd_for_events(
      /* events= */ POLLIN,
      /* fd= */ pipefd[0],
      /* timeout_ms= */ POLL_AUDIO_STATUS_TIMEOUT_MS);

  if (rc < 0) {
    syslog(LOG_WARNING, "%s: failed to wait for results", __func__);
    goto leave;
  }

  uint8_t codec_id = 0;
  ssize_t nread = read(pipefd[0], &codec_id, sizeof(codec_id));
  if (nread != sizeof(codec_id)) {
    syslog(LOG_WARNING, "%s: failed to read from pipe, rc = %d", __func__,
           errno);
    rc = -errno;
    goto leave;
  }

  if (codec_id != 0) {
    syslog(LOG_WARNING, "%s: unexpected codec_id: %u", __func__, codec_id);
    rc = -EBADE;
    goto leave;
  }

  rc = 0;

leave:
  close(pipefd[0]);
  close(pipefd[1]);

  return rc;
}

int floss_media_hfp_set_volume(struct fl_media* fm,
                               unsigned int volume,
                               const char* addr) {
  RET_IF_HAVE_FUZZER(0);

  DBusMessage *method_call, *reply;
  DBusError dbus_error;
  uint8_t vol = volume;

  syslog(LOG_DEBUG, "floss_media_hfp_set_volume: %d %s", volume, addr);

  method_call = dbus_message_new_method_call(
      BT_SERVICE_NAME, fm->obj_path, BT_MEDIA_INTERFACE, "SetHfpVolume");
  if (!method_call) {
    return -ENOMEM;
  }

  if (!dbus_message_append_args(method_call, DBUS_TYPE_BYTE, &vol,
                                DBUS_TYPE_STRING, &addr, DBUS_TYPE_INVALID)) {
    dbus_message_unref(method_call);
    return -ENOMEM;
  }

  dbus_error_init(&dbus_error);

  reply = dbus_connection_send_with_reply_and_block(
      fm->conn, method_call, DBUS_TIMEOUT_USE_DEFAULT, &dbus_error);
  if (!reply) {
    syslog(LOG_ERR, "Failed to send SetVolume: %s", dbus_error.message);
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

  dbus_message_unref(reply);

  return 0;
}

int floss_media_hfp_suspend(struct fl_media* fm) {
  if (fm != active_fm) {
    syslog(LOG_WARNING, "Invalid fl_media instance to suspend hfp");
    return 0;
  }

  if (fm->hfp == NULL) {
    syslog(LOG_WARNING, "Invalid hfp instance to suspend");
    return 0;
  }
  bt_io_manager_remove_iodev(fm->bt_io_mgr,
                             cras_floss_hfp_get_input_iodev(fm->hfp));
  bt_io_manager_remove_iodev(fm->bt_io_mgr,
                             cras_floss_hfp_get_output_iodev(fm->hfp));

  // TODO(b/286330209): ideally this should be covered by the BT stack
  floss_media_disconnect_device(fm, cras_floss_hfp_get_addr(fm->hfp));

  cras_floss_hfp_destroy(fm->hfp);
  fm->hfp = NULL;
  return 0;
}

int floss_media_a2dp_reset_active_device(struct fl_media* fm) {
  RET_IF_HAVE_FUZZER(0);

  DBusMessage *method_call, *reply;
  DBusError dbus_error;

  syslog(LOG_DEBUG, "floss_media_reset_active_device");

  method_call = dbus_message_new_method_call(
      BT_SERVICE_NAME, fm->obj_path, BT_MEDIA_INTERFACE, "ResetActiveDevice");
  if (!method_call) {
    return -ENOMEM;
  }

  dbus_error_init(&dbus_error);
  reply = dbus_connection_send_with_reply_and_block(
      active_fm->conn, method_call, DBUS_TIMEOUT_USE_DEFAULT, &dbus_error);
  if (!reply) {
    syslog(LOG_ERR, "Failed to send ResetActiveDevice : %s",
           dbus_error.message);
    dbus_error_free(&dbus_error);
    dbus_message_unref(method_call);
    return -EIO;
  }

  dbus_message_unref(method_call);

  if (dbus_message_get_type(reply) == DBUS_MESSAGE_TYPE_ERROR) {
    syslog(LOG_ERR, "ResetActiveDevice returned error: %s",
           dbus_message_get_error_name(reply));
    dbus_message_unref(reply);
    return -EIO;
  }

  dbus_message_unref(reply);

  return 0;
}

int floss_media_a2dp_set_active_device(struct fl_media* fm, const char* addr) {
  RET_IF_HAVE_FUZZER(0);

  DBusMessage *method_call, *reply;
  DBusError dbus_error;

  syslog(LOG_DEBUG, "floss_media_set_active_device");

  method_call = dbus_message_new_method_call(
      BT_SERVICE_NAME, fm->obj_path, BT_MEDIA_INTERFACE, "SetActiveDevice");
  if (!method_call) {
    return -ENOMEM;
  }

  if (!dbus_message_append_args(method_call, DBUS_TYPE_STRING, &addr,
                                DBUS_TYPE_INVALID)) {
    dbus_message_unref(method_call);
    return -ENOMEM;
  }

  dbus_error_init(&dbus_error);
  reply = dbus_connection_send_with_reply_and_block(
      active_fm->conn, method_call, DBUS_TIMEOUT_USE_DEFAULT, &dbus_error);
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

  dbus_message_unref(reply);

  return 0;
}

int floss_media_a2dp_set_audio_config(struct fl_media* fm,
                                      const char* addr,
                                      int codec_type,
                                      int sample_rate,
                                      int bits_per_sample,
                                      int channel_mode) {
  RET_IF_HAVE_FUZZER(0);

  int rc;

  dbus_int32_t dbus_codec_type = codec_type;
  dbus_int32_t dbus_sample_rate = sample_rate;
  dbus_int32_t dbus_bits_per_sample = bits_per_sample;
  dbus_int32_t dbus_channel_mode = channel_mode;

  syslog(LOG_DEBUG, "floss_media_a2dp_set_audio_config %d", codec_type);

  if (!fm) {
    syslog(LOG_WARNING, "%s: Floss media not started", __func__);
    return -EINVAL;
  }

  DBusMessage* set_audio_config;
  rc = create_dbus_method_call(&set_audio_config,
                               /* dest= */ BT_SERVICE_NAME,
                               /* path= */ fm->obj_path,
                               /* iface= */ BT_MEDIA_INTERFACE,
                               /* method_name= */ "SetAudioConfig",
                               /* num_args= */ 5,
                               /* arg1= */ DBUS_TYPE_STRING, &addr,
                               /* arg2= */ DBUS_TYPE_UINT32, &dbus_codec_type,
                               /* arg3= */ DBUS_TYPE_INT32, &dbus_sample_rate,
                               /* arg4= */ DBUS_TYPE_INT32,
                               &dbus_bits_per_sample,
                               /* arg5= */ DBUS_TYPE_INT32, &dbus_channel_mode);

  if (rc < 0) {
    return rc;
  }

  dbus_bool_t response = FALSE;
  rc = call_method_and_parse_reply(
      /* conn= */ fm->conn,
      /* method_call= */ set_audio_config,
      /* dbus_ret_type= */ DBUS_TYPE_BOOLEAN,
      /* dbus_ret_value_ptr= */ &response,
      /* log_on_error= */ false);

  dbus_message_unref(set_audio_config);

  if (rc < 0) {
    return rc;
  }

  if (response == FALSE) {
    syslog(LOG_WARNING, "SetAudioConfig was rejected");
    return -EPERM;
  }

  return 0;
}

int floss_media_a2dp_start_audio_request(struct fl_media* fm,
                                         const char* addr) {
  RET_IF_HAVE_FUZZER(0);

  int rc = 0;

  syslog(LOG_DEBUG, "%s: %s", __func__, addr);

  if (!fm) {
    syslog(LOG_WARNING, "%s: Floss media not started", __func__);
    return -EINVAL;
  }

  int pipefd[2];
  if (pipe(pipefd) == -1) {
    syslog(LOG_WARNING, "%s: failed to create pipe, rc = %d", __func__, errno);
    return -errno;
  }

  DBusMessage* start_audio_request;
  rc = create_dbus_method_call(&start_audio_request,
                               /* dest= */ BT_SERVICE_NAME,
                               /* path= */ fm->obj_path,
                               /* iface= */ BT_MEDIA_INTERFACE,
                               /* method_name= */ "StartAudioRequest",
                               /* num_args= */ 1,
                               /* arg1= */ DBUS_TYPE_UNIX_FD, &pipefd[1]);

  if (rc < 0) {
    goto leave;
  }

  dbus_bool_t response = FALSE;
  rc = call_method_and_parse_reply(
      /* conn= */ fm->conn,
      /* method_call= */ start_audio_request,
      /* dbus_ret_type= */ DBUS_TYPE_BOOLEAN,
      /* dbus_ret_value_ptr= */ &response,
      /* log_on_error= */ true);

  dbus_message_unref(start_audio_request);

  if (rc < 0) {
    goto leave;
  }

  if (response == FALSE) {
    syslog(LOG_WARNING, "%s: request was rejected", __func__);
    rc = -EPERM;
    goto leave;
  }

  rc = poll_fd_for_events(
      /* events= */ POLLIN,
      /* fd= */ pipefd[0],
      /* timeout_ms= */ POLL_AUDIO_STATUS_TIMEOUT_MS);

  if (rc < 0) {
    syslog(LOG_WARNING, "%s: failed to wait for results", __func__);
    goto leave;
  }

  uint8_t started = 0;
  ssize_t nread = read(pipefd[0], &started, sizeof(started));
  if (nread != sizeof(started)) {
    syslog(LOG_WARNING, "%s: failed to read from pipe, rc = %d", __func__,
           errno);
    rc = -errno;
    goto leave;
  }

  if (started == 0) {
    syslog(LOG_WARNING, "%s: unexpected status: %u", __func__, started);
    rc = -EBADE;
    goto leave;
  }

  rc = 0;

leave:
  close(pipefd[0]);
  close(pipefd[1]);

  return rc;
}

int floss_media_a2dp_stop_audio_request(struct fl_media* fm, const char* addr) {
  RET_IF_HAVE_FUZZER(0);

  int rc = 0;

  syslog(LOG_DEBUG, "%s: %s", __func__, addr);

  if (!fm) {
    syslog(LOG_WARNING, "%s: Floss media not started", __func__);
    return -EINVAL;
  }

  int pipefd[2];
  if (pipe(pipefd) == -1) {
    syslog(LOG_WARNING, "%s: failed to create pipe, rc = %d", __func__, errno);
    return -errno;
  }

  DBusMessage* stop_audio_request;
  rc = create_dbus_method_call(&stop_audio_request,
                               /* dest= */ BT_SERVICE_NAME,
                               /* path= */ fm->obj_path,
                               /* iface= */ BT_MEDIA_INTERFACE,
                               /* method_name= */ "StopAudioRequest",
                               /* num_args= */ 1,
                               /* arg1= */ DBUS_TYPE_UNIX_FD, &pipefd[1]);

  if (rc < 0) {
    goto leave;
  }

  rc = call_method_and_parse_reply(
      /* conn= */ fm->conn,
      /* method_call= */ stop_audio_request,
      /* dbus_ret_type= */ DBUS_TYPE_INVALID,
      /* dbus_ret_value_ptr= */ NULL,
      /* log_on_error= */ true);

  dbus_message_unref(stop_audio_request);

  if (rc < 0) {
    goto leave;
  }

  rc = poll_fd_for_events(
      /* events= */ POLLIN,
      /* fd= */ pipefd[0],
      /* timeout_ms= */ POLL_AUDIO_STATUS_TIMEOUT_MS);

  if (rc < 0) {
    syslog(LOG_WARNING, "%s: failed to wait for results", __func__);
    goto leave;
  }

  uint8_t started = 0;
  ssize_t nread = read(pipefd[0], &started, sizeof(started));
  if (nread != sizeof(started)) {
    syslog(LOG_WARNING, "%s: failed to read from pipe, rc = %d", __func__,
           errno);
    rc = -errno;
    goto leave;
  }

  if (started != 0) {
    syslog(LOG_WARNING, "%s: unexpected status: %u", __func__, started);
    rc = -EBADE;
    goto leave;
  }

  rc = 0;

leave:
  close(pipefd[0]);
  close(pipefd[1]);

  return rc;
}

int floss_media_a2dp_suspend(struct fl_media* fm) {
  if (fm != active_fm) {
    syslog(LOG_WARNING, "Invalid fl_media instance to suspend a2dp");
    return 0;
  }

  if (fm->a2dp == NULL) {
    syslog(LOG_WARNING, "Invalid a2dp instance to suspend");
    return 0;
  }

  bt_io_manager_remove_iodev(fm->bt_io_mgr,
                             cras_floss_a2dp_get_iodev(fm->a2dp));

  // TODO(b/286330209): ideally this should be covered by the BT stack
  floss_media_disconnect_device(fm, cras_floss_a2dp_get_addr(fm->a2dp));

  cras_floss_a2dp_destroy(fm->a2dp);
  fm->a2dp = NULL;
  return 0;
}

static bool get_presentation_position_result(
    DBusMessage* message,
    uint64_t* remote_delay_report_ns,
    uint64_t* total_bytes_read,
    struct timespec* data_position_ts) {
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
    const char* key;

    if (dbus_message_iter_get_arg_type(&dict) != DBUS_TYPE_DICT_ENTRY) {
      syslog(LOG_ERR, "entry not dictionary");
      return FALSE;
    }

    dbus_message_iter_recurse(&dict, &entry);
    if (dbus_message_iter_get_arg_type(&entry) != DBUS_TYPE_STRING) {
      syslog(LOG_ERR, "entry not string");
      return FALSE;
    }

    dbus_message_iter_get_basic(&entry, &key);
    dbus_message_iter_next(&entry);

    if (dbus_message_iter_get_arg_type(&entry) != DBUS_TYPE_VARIANT) {
      return FALSE;
    }

    dbus_message_iter_recurse(&entry, &var);
    if (strcasecmp(key, "total_bytes_read") == 0) {
      if (dbus_message_iter_get_arg_type(&var) != DBUS_TYPE_UINT64) {
        return FALSE;
      }

      dbus_message_iter_get_basic(&var, &bytes);
    } else if (strcasecmp(key, "remote_delay_report_ns") == 0) {
      if (dbus_message_iter_get_arg_type(&var) != DBUS_TYPE_UINT64) {
        return FALSE;
      }

      dbus_message_iter_get_basic(&var, &delay_ns);
    } else if (strcasecmp(key, "data_position_sec") == 0) {
      if (dbus_message_iter_get_arg_type(&var) != DBUS_TYPE_INT64) {
        return FALSE;
      }

      dbus_message_iter_get_basic(&var, &data_position_sec);
    } else if (strcasecmp(key, "data_position_nsec") == 0) {
      if (dbus_message_iter_get_arg_type(&var) != DBUS_TYPE_INT32) {
        return FALSE;
      }

      dbus_message_iter_get_basic(&var, &data_position_nsec);
    } else {
      syslog(LOG_WARNING, "%s not supported, ignoring", key);
    }

    dbus_message_iter_next(&dict);
  }

  *total_bytes_read = bytes;
  *remote_delay_report_ns = delay_ns;
  data_position_ts->tv_sec = data_position_sec;
  data_position_ts->tv_nsec = data_position_nsec;
  return true;
}

int floss_media_a2dp_get_presentation_position(
    struct fl_media* fm,
    uint64_t* remote_delay_report_ns,
    uint64_t* total_bytes_read,
    struct timespec* data_position_ts) {
  RET_IF_HAVE_FUZZER(0);

  DBusMessage *method_call, *reply;
  DBusError dbus_error;

  method_call = dbus_message_new_method_call(BT_SERVICE_NAME, fm->obj_path,
                                             BT_MEDIA_INTERFACE,
                                             "GetPresentationPosition");
  if (!method_call) {
    return -ENOMEM;
  }

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
                                        total_bytes_read, data_position_ts)) {
    syslog(LOG_ERR, "GetPresentationPosition returned invalid results");
    dbus_message_unref(reply);
    return -EIO;
  }

  dbus_message_unref(reply);

  return 0;
}

int floss_media_a2dp_set_volume(struct fl_media* fm, unsigned int volume) {
  RET_IF_HAVE_FUZZER(0);

  DBusMessage *method_call, *reply;
  DBusError dbus_error;
  uint8_t absolute_volume = volume;

  syslog(LOG_DEBUG, "floss_media_a2dp_set_volume: %d", absolute_volume);

  method_call = dbus_message_new_method_call(BT_SERVICE_NAME, fm->obj_path,
                                             BT_MEDIA_INTERFACE, "SetVolume");
  if (!method_call) {
    return -ENOMEM;
  }

  if (!dbus_message_append_args(method_call, DBUS_TYPE_BYTE, &absolute_volume,
                                DBUS_TYPE_INVALID)) {
    dbus_message_unref(method_call);
    return -ENOMEM;
  }

  dbus_error_init(&dbus_error);

  reply = dbus_connection_send_with_reply_and_block(
      fm->conn, method_call, DBUS_TIMEOUT_USE_DEFAULT, &dbus_error);
  if (!reply) {
    syslog(LOG_ERR, "Failed to send SetVolume: %s", dbus_error.message);
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

  dbus_message_unref(reply);

  return 0;
}

static void floss_on_register_callback(DBusPendingCall* pending_call,
                                       void* data) {
  DBusMessage* reply;

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

static int floss_media_register_callback(DBusConnection* conn,
                                         const struct fl_media* fm) {
  const char* bt_media_object_path = CRAS_BT_MEDIA_OBJECT_PATH;
  DBusMessage* method_call;
  DBusPendingCall* pending_call;

  method_call = dbus_message_new_method_call(
      BT_SERVICE_NAME, fm->obj_path, BT_MEDIA_INTERFACE, "RegisterCallback");
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

  if (!dbus_pending_call_set_notify(pending_call, floss_on_register_callback,
                                    conn, NULL)) {
    dbus_pending_call_cancel(pending_call);
    dbus_pending_call_unref(pending_call);
    return -ENOMEM;
  }

  return 0;
}

static void floss_on_register_telephony_callback(DBusPendingCall* pending_call,
                                                 void* data) {
  DBusMessage* reply;

  reply = dbus_pending_call_steal_reply(pending_call);
  dbus_pending_call_unref(pending_call);

  if (dbus_message_get_type(reply) == DBUS_MESSAGE_TYPE_ERROR) {
    syslog(LOG_WARNING, "RegisterTelephonyCallback returned error: %s",
           dbus_message_get_error_name(reply));
    dbus_message_unref(reply);
    return;
  }

  dbus_message_unref(reply);
}

static int floss_media_register_telephony_callback(DBusConnection* conn,
                                                   const struct fl_media* fm) {
  const char* bt_media_object_path = CRAS_BT_MEDIA_OBJECT_PATH;
  DBusMessage* method_call;
  DBusPendingCall* pending_call;

  method_call = dbus_message_new_method_call(
      BT_SERVICE_NAME, fm->obj_telephony_path, BT_TELEPHONY_INTERFACE,
      "RegisterTelephonyCallback");
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
          pending_call, floss_on_register_telephony_callback, conn, NULL)) {
    dbus_pending_call_cancel(pending_call);
    dbus_pending_call_unref(pending_call);
    return -ENOMEM;
  }

  return 0;
}

static struct cras_fl_a2dp_codec_config* parse_a2dp_codec(
    DBusMessageIter* codec) {
  DBusMessageIter dict;
  const char* key = NULL;
  dbus_int32_t bps, channels, priority, type, rate;
  bps = channels = priority = type = rate = -1;

  dbus_message_iter_recurse(codec, &dict);
  while (dbus_message_iter_get_arg_type(&dict) != DBUS_TYPE_INVALID) {
    DBusMessageIter entry, var;
    if (dbus_message_iter_get_arg_type(&dict) != DBUS_TYPE_DICT_ENTRY) {
      syslog(LOG_ERR, "entry not dictionary");
      return NULL;
    }

    dbus_message_iter_recurse(&dict, &entry);
    if (dbus_message_iter_get_arg_type(&entry) != DBUS_TYPE_STRING) {
      syslog(LOG_ERR, "entry not string");
      return NULL;
    }

    dbus_message_iter_get_basic(&entry, &key);
    dbus_message_iter_next(&entry);
    if (dbus_message_iter_get_arg_type(&entry) != DBUS_TYPE_VARIANT) {
      syslog(LOG_ERR, "entry not variant");
      return NULL;
    }
    dbus_message_iter_recurse(&entry, &var);
    if (strcasecmp(key, "bits_per_sample") == 0) {
      if (dbus_message_iter_get_arg_type(&var) != DBUS_TYPE_INT32) {
        goto invald_dict_value;
      }

      dbus_message_iter_get_basic(&var, &bps);
    } else if (strcasecmp(key, "channel_mode") == 0) {
      if (dbus_message_iter_get_arg_type(&var) != DBUS_TYPE_INT32) {
        goto invald_dict_value;
      }

      dbus_message_iter_get_basic(&var, &channels);
    } else if (strcasecmp(key, "codec_priority") == 0) {
      if (dbus_message_iter_get_arg_type(&var) != DBUS_TYPE_INT32) {
        goto invald_dict_value;
      }

      dbus_message_iter_get_basic(&var, &priority);
    } else if (strcasecmp(key, "codec_type") == 0) {
      if (dbus_message_iter_get_arg_type(&var) != DBUS_TYPE_INT32) {
        goto invald_dict_value;
      }

      dbus_message_iter_get_basic(&var, &type);
    } else if (strcasecmp(key, "sample_rate") == 0) {
      if (dbus_message_iter_get_arg_type(&var) != DBUS_TYPE_INT32) {
        goto invald_dict_value;
      }

      dbus_message_iter_get_basic(&var, &rate);
    } else if (strcasecmp(key, "codec_specific_1") == 0) {
      if (dbus_message_iter_get_arg_type(&var) != DBUS_TYPE_INT64) {
        goto invald_dict_value;
      }

      // No active use case yet
    } else if (strcasecmp(key, "codec_specific_2") == 0) {
      if (dbus_message_iter_get_arg_type(&var) != DBUS_TYPE_INT64) {
        goto invald_dict_value;
      }

      // No active use case yet
    } else if (strcasecmp(key, "codec_specific_3") == 0) {
      if (dbus_message_iter_get_arg_type(&var) != DBUS_TYPE_INT64) {
        goto invald_dict_value;
      }

      // No active use case yet
    } else if (strcasecmp(key, "codec_specific_4") == 0) {
      if (dbus_message_iter_get_arg_type(&var) != DBUS_TYPE_INT64) {
        goto invald_dict_value;
      }

      // No active use case yet
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

  return cras_floss_a2dp_codec_create(bps, channels, priority, type, rate);
invald_dict_value:
  syslog(LOG_ERR, "Invalid value type for key %s", key);
  return NULL;
}

static struct cras_fl_a2dp_codec_config* parse_a2dp_codecs(
    DBusMessageIter* codecs_iter) {
  struct cras_fl_a2dp_codec_config *codecs = NULL, *config;
  DBusMessageIter array_iter;

  dbus_message_iter_recurse(codecs_iter, &array_iter);

  while (dbus_message_iter_get_arg_type(&array_iter) != DBUS_TYPE_INVALID) {
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
             config->codec_priority, config->codec_type, config->sample_rate);
    }
    dbus_message_iter_next(&array_iter);
  }

  return codecs;
}

static bool parse_bluetooth_audio_device_added(
    DBusMessage* message,
    const char** addr,
    const char** name,
    struct cras_fl_a2dp_codec_config** codecs,
    dbus_int32_t* hfp_cap,
    dbus_bool_t* abs_vol_supported) {
  DBusMessageIter iter, dict;
  const char *remote_name, *address;

  dbus_message_iter_init(message, &iter);

  if (!dbus_message_has_signature(message, "a{sv}")) {
    syslog(LOG_ERR, "Received wrong format BluetoothAudioDeviceAdded signal");
    return FALSE;
  }

  // Default to False if not provided.
  *abs_vol_supported = FALSE;

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
    if (strcasecmp(key, "name") == 0) {
      if (dbus_message_iter_get_arg_type(&var) != DBUS_TYPE_STRING) {
        return FALSE;
      }

      dbus_message_iter_get_basic(&var, &remote_name);
    } else if (strcasecmp(key, "address") == 0) {
      if (dbus_message_iter_get_arg_type(&var) != DBUS_TYPE_STRING) {
        return FALSE;
      }

      dbus_message_iter_get_basic(&var, &address);
    } else if (strcasecmp(key, "a2dp_caps") == 0) {
      if (dbus_message_iter_get_arg_type(&var) != DBUS_TYPE_ARRAY) {
        return FALSE;
      }

      *codecs = parse_a2dp_codecs(&var);
    } else if (strcasecmp(key, "hfp_cap") == 0) {
      if (dbus_message_iter_get_arg_type(&var) != DBUS_TYPE_INT32) {
        return FALSE;
      }

      dbus_message_iter_get_basic(&var, hfp_cap);
    } else if (strcasecmp(key, "absolute_volume") == 0) {
      if (dbus_message_iter_get_arg_type(&var) != DBUS_TYPE_BOOLEAN) {
        return FALSE;
      }

      dbus_message_iter_get_basic(&var, abs_vol_supported);
    } else {
      syslog(LOG_WARNING, "%s not supported, ignoring", key);
    }
    dbus_message_iter_next(&dict);
  }

  if (remote_name == NULL || address == NULL) {
    return FALSE;
  }

  *name = remote_name;
  *addr = address;
  return TRUE;
}

static DBusHandlerResult handle_bt_media_callback(DBusConnection* conn,
                                                  DBusMessage* message,
                                                  void* arg) {
  int rc;
  const char *addr = NULL, *name = NULL;
  DBusError dbus_error;
  dbus_int32_t hfp_cap;
  dbus_bool_t abs_vol_supported;
  dbus_int32_t group_id;
  uint8_t telephony_event;
  uint8_t telephony_state;
  struct cras_fl_a2dp_codec_config* codecs = NULL;
  uint8_t volume;

  syslog(LOG_DEBUG, "Bt Media callback message: %s %s %s",
         dbus_message_get_path(message), dbus_message_get_interface(message),
         dbus_message_get_member(message));

  if (dbus_message_is_method_call(message, BT_MEDIA_CALLBACK_INTERFACE,
                                  "OnBluetoothAudioDeviceAdded")) {
    dbus_error_init(&dbus_error);
    if (!parse_bluetooth_audio_device_added(message, &addr, &name, &codecs,
                                            &hfp_cap, &abs_vol_supported)) {
      return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
    }

    syslog(LOG_DEBUG, "OnBluetoothAudioDeviceAdded %s %s", addr, name);

    if (!active_fm) {
      syslog(LOG_WARNING, "Floss media object not ready");
      return DBUS_HANDLER_RESULT_HANDLED;
    }

    rc = handle_on_bluetooth_device_added(active_fm, addr, name, codecs,
                                          hfp_cap, abs_vol_supported);
    if (rc) {
      syslog(LOG_ERR, "Error occurred in adding bluetooth device %d", rc);
    }

    struct cras_fl_a2dp_codec_config* codec;
    LL_FOREACH (codecs, codec) {
      LL_DELETE(codecs, codec);
      free(codec);
    }

    return DBUS_HANDLER_RESULT_HANDLED;
  } else if (dbus_message_is_method_call(message, BT_MEDIA_CALLBACK_INTERFACE,
                                         "OnBluetoothAudioDeviceRemoved")) {
    rc = get_single_arg(message, DBUS_TYPE_STRING, &addr);
    if (rc) {
      syslog(LOG_ERR, "Failed to get addr from OnBluetoothAudioDeviceRemoved");
      return rc;
    }

    syslog(LOG_DEBUG, "OnBluetoothAudioDeviceRemoved %s", addr);

    if (!active_fm) {
      syslog(LOG_ERR, "fl_media hasn't started or stopped");
      return DBUS_HANDLER_RESULT_HANDLED;
    }

    rc = handle_on_bluetooth_device_removed(active_fm, addr);
    if (rc) {
      syslog(LOG_ERR, "Error occurred in removing bluetooth device %d", rc);
    }
    return DBUS_HANDLER_RESULT_HANDLED;
  } else if (dbus_message_is_method_call(message, BT_MEDIA_CALLBACK_INTERFACE,
                                         "OnAbsoluteVolumeSupportedChanged")) {
    rc = get_single_arg(message, DBUS_TYPE_BOOLEAN, &abs_vol_supported);
    if (rc) {
      syslog(LOG_ERR, "Failed to get support from OnAvrcpConnected");
      return rc;
    }

    if (!active_fm) {
      syslog(LOG_ERR, "fl_media hasn't started or stopped");
      return DBUS_HANDLER_RESULT_HANDLED;
    }

    syslog(LOG_DEBUG, "OnAbsoluteVolumeSupportedChanged %d", abs_vol_supported);
    rc = handle_on_absolute_volume_supported_changed(active_fm,
                                                     abs_vol_supported);
    if (rc) {
      syslog(LOG_ERR,
             "Error occurred in setting absolute volume supported change %d",
             rc);
    }
    return DBUS_HANDLER_RESULT_HANDLED;
  } else if (dbus_message_is_method_call(message, BT_MEDIA_CALLBACK_INTERFACE,
                                         "OnAbsoluteVolumeChanged")) {
    rc = get_single_arg(message, DBUS_TYPE_BYTE, &volume);
    if (rc) {
      syslog(LOG_ERR, "Failed to get volume from OnAbsoluteVolumeChanged");
      return rc;
    }

    if (!active_fm) {
      syslog(LOG_ERR, "fl_media hasn't started or stopped");
      return DBUS_HANDLER_RESULT_HANDLED;
    }
    syslog(LOG_DEBUG, "OnAbsoluteVolumeChanged %u", volume);

    rc = handle_on_absolute_volume_changed(active_fm, volume);
    if (rc) {
      syslog(LOG_ERR, "Error occurred in updating hardware volume %d", rc);
    }
    return DBUS_HANDLER_RESULT_HANDLED;
  } else if (dbus_message_is_method_call(message, BT_MEDIA_CALLBACK_INTERFACE,
                                         "OnHfpVolumeChanged")) {
    dbus_error_init(&dbus_error);
    if (!dbus_message_get_args(message, &dbus_error, DBUS_TYPE_BYTE, &volume,
                               DBUS_TYPE_STRING, &addr, DBUS_TYPE_INVALID)) {
      syslog(LOG_ERR,
             "Failed to get volume and address from OnHfpVolumeChanged: %s",
             dbus_error.message);
      dbus_error_free(&dbus_error);
      return DBUS_HANDLER_RESULT_HANDLED;
    }

    if (!active_fm) {
      syslog(LOG_ERR, "fl_media hasn't started or stopped");
      return DBUS_HANDLER_RESULT_HANDLED;
    }
    syslog(LOG_DEBUG, "OnHfpVolumeChanged %u", volume);

    rc = handle_on_hfp_volume_changed(active_fm, addr, volume);
    if (rc) {
      syslog(LOG_ERR, "Error occurred in updating hfp volume %d", rc);
    }
    return DBUS_HANDLER_RESULT_HANDLED;
  } else if (dbus_message_is_method_call(message, BT_MEDIA_CALLBACK_INTERFACE,
                                         "OnHfpAudioDisconnected")) {
    dbus_error_init(&dbus_error);
    if (!dbus_message_get_args(message, &dbus_error, DBUS_TYPE_STRING, &addr,
                               DBUS_TYPE_INVALID)) {
      syslog(LOG_ERR, "Failed to get address from OnHfpAudioDisconnected: %s",
             dbus_error.message);
      dbus_error_free(&dbus_error);
      return DBUS_HANDLER_RESULT_HANDLED;
    }

    if (!active_fm) {
      syslog(LOG_ERR, "fl_media hasn't started or stopped");
      return DBUS_HANDLER_RESULT_HANDLED;
    }
    syslog(LOG_DEBUG, "OnHfpAudioDisconnected");

    rc = handle_on_hfp_audio_disconnected(active_fm, addr);
    if (rc) {
      syslog(LOG_ERR, "Error occurred in handling hfp audio disconnection %d",
             rc);
    }
    return DBUS_HANDLER_RESULT_HANDLED;
  } else if (dbus_message_is_method_call(message,
                                         BT_TELEPHONY_CALLBACK_INTERFACE,
                                         "OnTelephonyEvent")) {
    dbus_error_init(&dbus_error);
    if (!dbus_message_get_args(message, &dbus_error, DBUS_TYPE_STRING, &addr,
                               DBUS_TYPE_BYTE, &telephony_event, DBUS_TYPE_BYTE,
                               &telephony_state, DBUS_TYPE_INVALID)) {
      syslog(LOG_ERR, "Failed to get args from OnTelephonyEvent: %s",
             dbus_error.message);
      dbus_error_free(&dbus_error);
      return DBUS_HANDLER_RESULT_HANDLED;
    }
    if (!active_fm) {
      syslog(LOG_ERR, "fl_media hasn't started or stopped");
      return DBUS_HANDLER_RESULT_HANDLED;
    }
    syslog(LOG_DEBUG, "OnTelephonyEvent: event: %u state: %u", telephony_event,
           telephony_state);
    rc = handle_on_hfp_telephony_event(active_fm, addr, telephony_event,
                                       telephony_state);
    if (rc) {
      syslog(LOG_ERR,
             "Error occurred in handling handle_on_hfp_telephony_event %d", rc);
    }
    return DBUS_HANDLER_RESULT_HANDLED;
  } else if (dbus_message_is_method_call(message, BT_MEDIA_CALLBACK_INTERFACE,
                                         "OnLeaGroupConnected")) {
    dbus_error_init(&dbus_error);
    if (!dbus_message_get_args(message, &dbus_error, DBUS_TYPE_INT32, &group_id,
                               DBUS_TYPE_STRING, &name, DBUS_TYPE_INVALID)) {
      syslog(LOG_ERR,
             "Failed to get group_id and name from OnLeaGroupConnected: %s",
             dbus_error.message);
      dbus_error_free(&dbus_error);
      return DBUS_HANDLER_RESULT_HANDLED;
    }

    syslog(LOG_DEBUG, "OnLeaGroupConnected %s %d", name, group_id);

    if (!active_fm) {
      syslog(LOG_WARNING, "Floss media object not ready");
      return DBUS_HANDLER_RESULT_HANDLED;
    }

    rc = handle_on_lea_group_connected(active_fm, name, group_id);
    if (rc) {
      syslog(LOG_ERR, "Error occured in adding LEA group %d", rc);
    }

    return DBUS_HANDLER_RESULT_HANDLED;
  } else if (dbus_message_is_method_call(message, BT_MEDIA_CALLBACK_INTERFACE,
                                         "OnLeaGroupDisconnected")) {
    rc = get_single_arg(message, DBUS_TYPE_INT32, &group_id);
    if (rc) {
      syslog(LOG_ERR, "Failed to get addr from OnLeaGroupDisconnected");
      return rc;
    }

    syslog(LOG_DEBUG, "OnLeaGroupDisconnected %d", group_id);

    if (!active_fm) {
      syslog(LOG_ERR, "fl_media hasn't started or stopped");
      return DBUS_HANDLER_RESULT_HANDLED;
    }

    rc = handle_on_lea_group_disconnected(active_fm, group_id);
    if (rc) {
      syslog(LOG_ERR, "Error occured in removing LEA group %d", rc);
    }
    return DBUS_HANDLER_RESULT_HANDLED;
  } else if (dbus_message_is_method_call(message, BT_MEDIA_CALLBACK_INTERFACE,
                                         "OnLeaGroupStatus")) {
    dbus_int32_t group_id, status;
    dbus_error_init(&dbus_error);
    if (!dbus_message_get_args(message, &dbus_error, DBUS_TYPE_INT32, &group_id,
                               DBUS_TYPE_INT32, &status, DBUS_TYPE_INVALID)) {
      syslog(LOG_ERR, "Failed to get args from OnLeaGroupStatus: %s",
             dbus_error.message);
      dbus_error_free(&dbus_error);
      return DBUS_HANDLER_RESULT_HANDLED;
    }

    if (!active_fm) {
      syslog(LOG_ERR, "fl_media hasn't started or stopped");
      return DBUS_HANDLER_RESULT_HANDLED;
    }
    syslog(LOG_DEBUG, "OnLeaGroupStatus %d, %d", group_id, status);

    rc = handle_on_lea_group_status(active_fm, group_id, status);
    if (rc) {
      syslog(LOG_ERR, "Error occured in updating group status %d", rc);
    }
    return DBUS_HANDLER_RESULT_HANDLED;
  } else if (dbus_message_is_method_call(message, BT_MEDIA_CALLBACK_INTERFACE,
                                         "OnLeaGroupNodeStatus")) {
    dbus_int32_t group_id, status;
    dbus_error_init(&dbus_error);
    if (!dbus_message_get_args(message, &dbus_error, DBUS_TYPE_STRING, &addr,
                               DBUS_TYPE_INT32, &group_id, DBUS_TYPE_INT32,
                               &status, DBUS_TYPE_INVALID)) {
      syslog(LOG_ERR, "Failed to get args from OnLeaGroupNodeStatus: %s",
             dbus_error.message);
      dbus_error_free(&dbus_error);
      return DBUS_HANDLER_RESULT_HANDLED;
    }

    if (!active_fm) {
      syslog(LOG_ERR, "fl_media hasn't started or stopped");
      return DBUS_HANDLER_RESULT_HANDLED;
    }
    syslog(LOG_DEBUG, "OnLeaGroupNodeStatus %s, %d, %d", addr, group_id,
           status);

    rc = handle_on_lea_group_node_status(active_fm, addr, group_id, status);
    if (rc) {
      syslog(LOG_ERR, "Error occured in updating group node status %d", rc);
    }
    return DBUS_HANDLER_RESULT_HANDLED;
  } else if (dbus_message_is_method_call(message, BT_MEDIA_CALLBACK_INTERFACE,
                                         "OnLeaAudioConf")) {
    uint8_t direction;
    dbus_int32_t group_id;
    dbus_uint32_t snk_audio_location;
    dbus_uint32_t src_audio_location;
    dbus_uint16_t available_contexts;

    dbus_error_init(&dbus_error);
    if (!dbus_message_get_args(message, &dbus_error, DBUS_TYPE_BYTE, &direction,
                               DBUS_TYPE_INT32, &group_id, DBUS_TYPE_UINT32,
                               &snk_audio_location, DBUS_TYPE_UINT32,
                               &src_audio_location, DBUS_TYPE_UINT16,
                               &available_contexts, DBUS_TYPE_INVALID)) {
      syslog(LOG_ERR, "Failed to get args from OnLeaAudioConf: %s",
             dbus_error.message);
      dbus_error_free(&dbus_error);
      return DBUS_HANDLER_RESULT_HANDLED;
    }

    if (!active_fm) {
      syslog(LOG_ERR, "fl_media hasn't started or stopped");
      return DBUS_HANDLER_RESULT_HANDLED;
    }
    syslog(LOG_DEBUG, "OnLeaAudioConf %u, %d, %u, %u, %u", direction, group_id,
           snk_audio_location, src_audio_location, available_contexts);

    rc = handle_on_lea_audio_conf(active_fm, direction, group_id,
                                  snk_audio_location, src_audio_location,
                                  available_contexts);
    if (rc) {
      syslog(LOG_ERR, "Error occured in updating audio conf %d", rc);
    }
    return DBUS_HANDLER_RESULT_HANDLED;
  } else if (dbus_message_is_method_call(message, BT_MEDIA_CALLBACK_INTERFACE,
                                         "OnLeaVcConnected")) {
    dbus_error_init(&dbus_error);
    if (!dbus_message_get_args(message, &dbus_error, DBUS_TYPE_STRING, &addr,
                               DBUS_TYPE_INT32, &group_id, DBUS_TYPE_INVALID)) {
      syslog(LOG_ERR, "Failed to get args from OnLeaVcConnected: %s",
             dbus_error.message);
      dbus_error_free(&dbus_error);
      return DBUS_HANDLER_RESULT_HANDLED;
    }

    if (!active_fm) {
      syslog(LOG_ERR, "fl_media hasn't started or stopped");
      return DBUS_HANDLER_RESULT_HANDLED;
    }
    syslog(LOG_DEBUG, "OnLeaVcConnected %s, %d", addr, group_id);

    rc = handle_on_lea_vc_connected(active_fm, addr, group_id);
    if (rc) {
      syslog(LOG_ERR, "Error occured in handling vc connection update %d", rc);
    }
    return DBUS_HANDLER_RESULT_HANDLED;
  } else if (dbus_message_is_method_call(message, BT_MEDIA_CALLBACK_INTERFACE,
                                         "OnLeaGroupVolumeChanged")) {
    dbus_error_init(&dbus_error);
    if (!dbus_message_get_args(message, &dbus_error, DBUS_TYPE_INT32, &group_id,
                               DBUS_TYPE_BYTE, &volume, DBUS_TYPE_INVALID)) {
      syslog(LOG_ERR, "Failed to get args from OnLeaGroupVolumeChanged: %s",
             dbus_error.message);
      dbus_error_free(&dbus_error);
      return DBUS_HANDLER_RESULT_HANDLED;
    }

    if (!active_fm) {
      syslog(LOG_ERR, "fl_media hasn't started or stopped");
      return DBUS_HANDLER_RESULT_HANDLED;
    }
    syslog(LOG_DEBUG, "OnLeaGroupVolumeChanged %d, %u", group_id, volume);

    rc = handle_on_lea_group_volume_changed(active_fm, group_id, volume);
    if (rc) {
      syslog(LOG_ERR, "Error occured in handling vc group volume update %d",
             rc);
    }
    return DBUS_HANDLER_RESULT_HANDLED;
  }
  return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
}

int floss_media_disconnect_device(struct fl_media* fm, const char* addr) {
  RET_IF_HAVE_FUZZER(0);

  int rc = 0;

  syslog(LOG_DEBUG, "%s: %s", __func__, addr);

  if (!fm) {
    syslog(LOG_WARNING, "%s: Floss media not started", __func__);
    return -EINVAL;
  }

  DBusMessage* disconnect;
  rc = create_dbus_method_call(&disconnect,
                               /* dest= */ BT_SERVICE_NAME,
                               /* path= */ fm->obj_path,
                               /* iface= */ BT_MEDIA_INTERFACE,
                               /* method_name= */ "Disconnect",
                               /* num_args= */ 1,
                               /* arg1= */ DBUS_TYPE_STRING, &addr);

  if (rc < 0) {
    return rc;
  }

  rc = call_method_and_parse_reply(
      /* conn= */ fm->conn,
      /* method_call= */ disconnect,
      /* dbus_ret_type= */ DBUS_TYPE_INVALID,
      /* dbus_ret_value_ptr= */ NULL,
      /* log_on_error= */ true);

  dbus_message_unref(disconnect);

  return rc;
}

int floss_media_lea_host_stop_audio_request(struct fl_media* fm) {
  RET_IF_HAVE_FUZZER(0);

  DBusMessage *method_call, *reply;
  DBusError dbus_error;

  syslog(LOG_DEBUG, "%s", __func__);

  method_call =
      dbus_message_new_method_call(BT_SERVICE_NAME, fm->obj_path,
                                   BT_MEDIA_INTERFACE, "HostStopAudioRequest");
  if (!method_call) {
    return -ENOMEM;
  }

  dbus_error_init(&dbus_error);

  reply = dbus_connection_send_with_reply_and_block(
      fm->conn, method_call, DBUS_TIMEOUT_USE_DEFAULT, &dbus_error);
  if (!reply) {
    syslog(LOG_ERR, "Failed to send HostStopAudioRequest: %s",
           dbus_error.message);
    dbus_error_free(&dbus_error);
    dbus_message_unref(method_call);
    return -EIO;
  }

  dbus_message_unref(method_call);

  if (dbus_message_get_type(reply) == DBUS_MESSAGE_TYPE_ERROR) {
    syslog(LOG_ERR, "HostStopAudioRequest returned error: %s",
           dbus_message_get_error_name(reply));
    dbus_message_unref(reply);
    return -EIO;
  }

  dbus_message_unref(reply);

  return 0;
}

static bool get_pcm_config_result(DBusMessage* message,
                                  uint32_t* data_interval_us,
                                  uint32_t* sample_rate,
                                  uint8_t* bits_per_sample,
                                  uint8_t* channels_count) {
  DBusMessageIter iter, dict;

  dbus_message_iter_init(message, &iter);
  if (dbus_message_iter_get_arg_type(&iter) != DBUS_TYPE_ARRAY) {
    syslog(LOG_ERR, "GetPcmConfig returned not array");
    return false;
  }

  dbus_message_iter_recurse(&iter, &dict);

  while (dbus_message_iter_get_arg_type(&dict) != DBUS_TYPE_INVALID) {
    DBusMessageIter entry, var;
    const char* key;

    if (dbus_message_iter_get_arg_type(&dict) != DBUS_TYPE_DICT_ENTRY) {
      syslog(LOG_ERR, "entry not dictionary");
      return FALSE;
    }

    dbus_message_iter_recurse(&dict, &entry);
    if (dbus_message_iter_get_arg_type(&entry) != DBUS_TYPE_STRING) {
      syslog(LOG_ERR, "entry not string");
      return FALSE;
    }

    dbus_message_iter_get_basic(&entry, &key);
    dbus_message_iter_next(&entry);

    if (dbus_message_iter_get_arg_type(&entry) != DBUS_TYPE_VARIANT) {
      return FALSE;
    }

    dbus_message_iter_recurse(&entry, &var);
    if (strcasecmp(key, "data_interval_us") == 0) {
      if (dbus_message_iter_get_arg_type(&var) != DBUS_TYPE_UINT32) {
        return FALSE;
      }

      dbus_message_iter_get_basic(&var, data_interval_us);
    } else if (strcasecmp(key, "sample_rate") == 0) {
      if (dbus_message_iter_get_arg_type(&var) != DBUS_TYPE_UINT32) {
        return FALSE;
      }

      dbus_message_iter_get_basic(&var, sample_rate);
    } else if (strcasecmp(key, "bits_per_sample") == 0) {
      if (dbus_message_iter_get_arg_type(&var) != DBUS_TYPE_BYTE) {
        return FALSE;
      }

      dbus_message_iter_get_basic(&var, bits_per_sample);
    } else if (strcasecmp(key, "channels_count") == 0) {
      if (dbus_message_iter_get_arg_type(&var) != DBUS_TYPE_BYTE) {
        return FALSE;
      }

      dbus_message_iter_get_basic(&var, channels_count);
    } else {
      syslog(LOG_WARNING, "%s not supported, ignoring", key);
    }

    dbus_message_iter_next(&dict);
  }

  return true;
}

int floss_media_lea_host_start_audio_request(struct fl_media* fm,
                                             uint32_t* data_interval_us,
                                             uint32_t* sample_rate,
                                             uint8_t* bits_per_sample,
                                             uint8_t* channels_count) {
  RET_IF_HAVE_FUZZER(0);

  syslog(LOG_DEBUG, "%s", __func__);

  int rc = 0;

  if (!fm) {
    syslog(LOG_WARNING, "%s: Floss media not started", __func__);
    return -EINVAL;
  }

  for (int retries = GET_LEA_AUDIO_STARTED_RETRIES; retries > 0;) {
    DBusMessage* start_audio_request;
    rc = create_dbus_method_call(&start_audio_request,
                                 /* dest= */ BT_SERVICE_NAME,
                                 /* path= */ fm->obj_path,
                                 /* iface= */ BT_MEDIA_INTERFACE,
                                 /* method_name= */ "HostStartAudioRequest",
                                 /* num_args= */ 0);

    if (rc < 0) {
      return rc;
    }

    dbus_bool_t response = FALSE;
    rc = call_method_and_parse_reply(
        /* conn= */ fm->conn,
        /* method_call= */ start_audio_request,
        /* dbus_ret_type= */ DBUS_TYPE_BOOLEAN,
        /* dbus_ret_value_ptr= */ &response,
        /* log_on_error= */ true);

    dbus_message_unref(start_audio_request);

    if (rc < 0) {
      return rc;
    }

    if (response == FALSE) {
      syslog(LOG_WARNING, "Failed to make request to HostStartAudioRequest.");
      return -EBUSY;
    }

    DBusMessage* get_host_stream_started;
    rc = create_dbus_method_call(&get_host_stream_started,
                                 /* dest= */ BT_SERVICE_NAME,
                                 /* path= */ fm->obj_path,
                                 /* iface= */ BT_MEDIA_INTERFACE,
                                 /* method_name= */ "GetHostStreamStarted",
                                 /* num_args= */ 0);

    if (rc < 0) {
      return rc;
    }

    int executed_retries = 0;

    dbus_int32_t started = 0;
    rc = retry_until_predicate_satisfied(
        /* conn= */ fm->conn,
        /* num_retries= */ retries,
        /* sleep_time_us= */ GET_LEA_AUDIO_STARTED_SLEEP_US,
        /* method_call= */ get_host_stream_started,
        /* dbus_ret_type= */ DBUS_TYPE_INT32,
        /* dbus_ret_value_ptr= */ &started,
        /* predicate= */ dbus_int32_is_nonzero,
        /* executed_retries= */ &executed_retries);

    dbus_message_unref(get_host_stream_started);

    retries -= executed_retries;

    if (started == FL_LEA_STREAM_STARTED_STATUS_STARTED) {
      break;
    }

    if (started == FL_LEA_STREAM_STARTED_STATUS_CANCELED) {
      syslog(LOG_DEBUG,
             "HostStartAudioRequest was cancelled after %d retries to wait for "
             "|started|, remaining attempts: %d",
             executed_retries, retries);
    }

    if (rc < 0) {
      return rc;
    }
  }

  DBusMessage* get_pcm_config;
  rc = create_dbus_method_call(&get_pcm_config,
                               /* dest= */ BT_SERVICE_NAME,
                               /* path= */ fm->obj_path,
                               /* iface= */ BT_MEDIA_INTERFACE,
                               /* method_name= */ "GetHostPcmConfig",
                               /* num_args= */ 0);

  if (rc < 0) {
    return rc;
  }

  DBusMessage* reply;
  rc = call_method_and_get_reply(
      /* conn= */ fm->conn,
      /* method_call= */ get_pcm_config,
      /* reply_ptr_ptr= */ &reply,
      /* log_on_error= */ true);

  dbus_message_unref(get_pcm_config);

  if (!get_pcm_config_result(reply, data_interval_us, sample_rate,
                             bits_per_sample, channels_count)) {
    syslog(LOG_ERR, "GetHostPcmConfig returned invalid results");
    dbus_message_unref(reply);
    return -EIO;
  }

  dbus_message_unref(reply);

  syslog(LOG_DEBUG,
         "%s -> {data_interval_us=%u, sample_rate=%u, bits_per_sample=%u, "
         "channels_count=%u}",
         __func__, *data_interval_us, *sample_rate, *bits_per_sample,
         *channels_count);

  return 0;
}

int floss_media_lea_source_metadata_changed(
    struct fl_media* fm,
    enum FL_LEA_AUDIO_USAGE usage,
    enum FL_LEA_AUDIO_CONTENT_TYPE content_type,
    double gain) {
  RET_IF_HAVE_FUZZER(0);

  if (!fm) {
    syslog(LOG_WARNING, "%s: Floss media not started", __func__);
    return -EINVAL;
  }

  syslog(LOG_DEBUG, "%s(usage=%u, content_type=%u, gain=%lf)", __func__, usage,
         content_type, gain);

  int rc = 0;

  dbus_int32_t dbus_usage = usage;
  dbus_int32_t dbus_content_type = content_type;

  DBusMessage* source_metadata_changed;
  rc = create_dbus_method_call(&source_metadata_changed,
                               /* dest= */ BT_SERVICE_NAME,
                               /* path= */ fm->obj_path,
                               /* iface= */ BT_MEDIA_INTERFACE,
                               /* method_name= */ "SourceMetadataChanged",
                               /* num_args= */ 3,
                               /* arg1= */ DBUS_TYPE_INT32, &dbus_usage,
                               /* arg2= */ DBUS_TYPE_INT32, &dbus_content_type,
                               /* arg3= */ DBUS_TYPE_DOUBLE, &gain);

  if (rc < 0) {
    return rc;
  }

  dbus_bool_t response = FALSE;
  rc = call_method_and_parse_reply(
      /* conn= */ fm->conn,
      /* method_call= */ source_metadata_changed,
      /* dbus_ret_type= */ DBUS_TYPE_BOOLEAN,
      /* dbus_ret_value_ptr= */ &response,
      /* log_on_error= */ true);

  dbus_message_unref(source_metadata_changed);

  if (rc < 0) {
    return rc;
  }

  if (response == FALSE) {
    syslog(LOG_WARNING, "Failed to make request to SourceMetadataChanged.");
    return -EBUSY;
  }

  return 0;
}

int floss_media_lea_sink_metadata_changed(struct fl_media* fm,
                                          enum FL_LEA_AUDIO_SOURCE source,
                                          double gain) {
  RET_IF_HAVE_FUZZER(0);

  if (!fm) {
    syslog(LOG_WARNING, "%s: Floss media not started", __func__);
    return -EINVAL;
  }

  syslog(LOG_DEBUG, "%s(source=%u, gain=%lf)", __func__, source, gain);

  int rc = 0;

  dbus_int32_t dbus_source = source;

  DBusMessage* sink_metadata_changed;
  rc = create_dbus_method_call(&sink_metadata_changed,
                               /* dest= */ BT_SERVICE_NAME,
                               /* path= */ fm->obj_path,
                               /* iface= */ BT_MEDIA_INTERFACE,
                               /* method_name= */ "SinkMetadataChanged",
                               /* num_args= */ 2,
                               /* arg1= */ DBUS_TYPE_INT32, &dbus_source,
                               /* arg2= */ DBUS_TYPE_DOUBLE, &gain);

  if (rc < 0) {
    return rc;
  }

  dbus_bool_t response = FALSE;
  rc = call_method_and_parse_reply(
      /* conn= */ fm->conn,
      /* method_call= */ sink_metadata_changed,
      /* dbus_ret_type= */ DBUS_TYPE_BOOLEAN,
      /* dbus_ret_value_ptr= */ &response,
      /* log_on_error= */ true);

  dbus_message_unref(sink_metadata_changed);

  if (rc < 0) {
    return rc;
  }

  if (response == FALSE) {
    syslog(LOG_WARNING, "Failed to make request to SinkMetadataChanged.");
    return -EBUSY;
  }

  return 0;
}

int floss_media_lea_peer_start_audio_request(struct fl_media* fm,
                                             uint32_t* data_interval_us,
                                             uint32_t* sample_rate,
                                             uint8_t* bits_per_sample,
                                             uint8_t* channels_count) {
  RET_IF_HAVE_FUZZER(0);

  syslog(LOG_DEBUG, "%s", __func__);

  if (!fm) {
    syslog(LOG_WARNING, "%s: Floss media not started", __func__);
    return -EINVAL;
  }

  int rc = 0;

  for (int retries = GET_LEA_AUDIO_STARTED_RETRIES; retries > 0;) {
    DBusMessage* start_audio_request;
    rc = create_dbus_method_call(&start_audio_request,
                                 /* dest= */ BT_SERVICE_NAME,
                                 /* path= */ fm->obj_path,
                                 /* iface= */ BT_MEDIA_INTERFACE,
                                 /* method_name= */ "PeerStartAudioRequest",
                                 /* num_args= */ 0);

    if (rc < 0) {
      return rc;
    }

    dbus_bool_t response = FALSE;
    rc = call_method_and_parse_reply(
        /* conn= */ fm->conn,
        /* method_call= */ start_audio_request,
        /* dbus_ret_type= */ DBUS_TYPE_BOOLEAN,
        /* dbus_ret_value_ptr= */ &response,
        /* log_on_error= */ true);

    dbus_message_unref(start_audio_request);

    if (rc < 0) {
      return rc;
    }

    if (response == FALSE) {
      syslog(LOG_WARNING, "Failed to make request to PeerStartAudioRequest.");
      return -EBUSY;
    }

    DBusMessage* get_peer_stream_started;
    rc = create_dbus_method_call(&get_peer_stream_started,
                                 /* dest= */ BT_SERVICE_NAME,
                                 /* path= */ fm->obj_path,
                                 /* iface= */ BT_MEDIA_INTERFACE,
                                 /* method_name= */ "GetPeerStreamStarted",
                                 /* num_args= */ 0);

    if (rc < 0) {
      return rc;
    }

    int executed_retries = 0;

    dbus_int32_t started = 0;
    rc = retry_until_predicate_satisfied(
        /* conn= */ fm->conn,
        /* num_retries= */ retries,
        /* sleep_time_us= */ GET_LEA_AUDIO_STARTED_SLEEP_US,
        /* method_call= */ get_peer_stream_started,
        /* dbus_ret_type= */ DBUS_TYPE_INT32,
        /* dbus_ret_value_ptr= */ &started,
        /* predicate= */ dbus_int32_is_nonzero,
        /* executed_retries= */ &executed_retries);

    dbus_message_unref(get_peer_stream_started);

    retries -= executed_retries;

    if (started == FL_LEA_STREAM_STARTED_STATUS_STARTED) {
      break;
    }

    if (started == FL_LEA_STREAM_STARTED_STATUS_CANCELED) {
      syslog(LOG_DEBUG,
             "PeerStartAudioRequest was cancelled after %d retries to wait for "
             "|started|, remaining attempts: %d",
             executed_retries, retries);
    }

    if (rc < 0) {
      return rc;
    }
  }

  DBusMessage* get_pcm_config;
  rc = create_dbus_method_call(&get_pcm_config,
                               /* dest= */ BT_SERVICE_NAME,
                               /* path= */ fm->obj_path,
                               /* iface= */ BT_MEDIA_INTERFACE,
                               /* method_name= */ "GetPeerPcmConfig",
                               /* num_args= */ 0);

  if (rc < 0) {
    return rc;
  }

  DBusMessage* reply;
  rc = call_method_and_get_reply(
      /* conn= */ fm->conn,
      /* method_call= */ get_pcm_config,
      /* reply_ptr_ptr= */ &reply,
      /* log_on_error= */ true);

  dbus_message_unref(get_pcm_config);

  if (!get_pcm_config_result(reply, data_interval_us, sample_rate,
                             bits_per_sample, channels_count)) {
    syslog(LOG_ERR, "GetPeerPcmConfig returned invalid results");
    dbus_message_unref(reply);
    return -EIO;
  }

  dbus_message_unref(reply);

  syslog(LOG_DEBUG,
         "%s -> {data_interval_us=%u, sample_rate=%u, bits_per_sample=%u, "
         "channels_count=%u}",
         __func__, *data_interval_us, *sample_rate, *bits_per_sample,
         *channels_count);

  return 0;
}

int floss_media_lea_peer_stop_audio_request(struct fl_media* fm) {
  RET_IF_HAVE_FUZZER(0);

  DBusMessage *method_call, *reply;
  DBusError dbus_error;

  syslog(LOG_DEBUG, "%s", __func__);

  method_call =
      dbus_message_new_method_call(BT_SERVICE_NAME, fm->obj_path,
                                   BT_MEDIA_INTERFACE, "PeerStopAudioRequest");
  if (!method_call) {
    return -ENOMEM;
  }

  dbus_error_init(&dbus_error);

  reply = dbus_connection_send_with_reply_and_block(
      fm->conn, method_call, DBUS_TIMEOUT_USE_DEFAULT, &dbus_error);
  if (!reply) {
    syslog(LOG_ERR, "Failed to send PeerStopAudioRequest: %s",
           dbus_error.message);
    dbus_error_free(&dbus_error);
    dbus_message_unref(method_call);
    return -EIO;
  }

  dbus_message_unref(method_call);

  if (dbus_message_get_type(reply) == DBUS_MESSAGE_TYPE_ERROR) {
    syslog(LOG_ERR, "PeerStopAudioRequest returned error: %s",
           dbus_message_get_error_name(reply));
    dbus_message_unref(reply);
    return -EIO;
  }

  dbus_message_unref(reply);

  return 0;
}

int floss_media_lea_set_active_group(struct fl_media* fm, int group_id) {
  RET_IF_HAVE_FUZZER(0);

  int rc = 0;

  syslog(LOG_DEBUG, "%s(group_id=%d)", __func__, group_id);

  if (!fm) {
    syslog(LOG_WARNING, "%s: Floss media not started", __func__);
    return -EINVAL;
  }

  dbus_int32_t dbus_group_id = group_id;

  if (group_id != FL_LEA_GROUP_NONE) {
    DBusMessage* get_group_stream_status;
    rc = create_dbus_method_call(&get_group_stream_status,
                                 /* dest= */ BT_SERVICE_NAME,
                                 /* path= */ fm->obj_path,
                                 /* iface= */ BT_MEDIA_INTERFACE,
                                 /* method_name= */ "GetGroupStreamStatus",
                                 /* num_args= */ 1,
                                 /* arg1= */ DBUS_TYPE_INT32, &dbus_group_id);

    if (rc < 0) {
      return rc;
    }

    dbus_int32_t status = -1;
    rc = retry_until_predicate_satisfied(
        /* conn=*/fm->conn,
        /* num_retries= */ LEA_AUDIO_OP_RETRIES,
        /* sleep_time_us= */ LEA_AUDIO_OP_US,
        /* method_call= */ get_group_stream_status,
        /* dbus_ret_type= */ DBUS_TYPE_INT32,
        /* dbus_ret_value_ptr= */ &status,
        /* predicate= */ dbus_int32_as_group_stream_status_is_idle,
        /* executed_retries= */ NULL);

    dbus_message_unref(get_group_stream_status);

    if (rc < 0) {
      syslog(LOG_WARNING,
             "%s: Failed to wait for group stream status to transition to idle",
             __func__);
    }
  }

  DBusMessage* set_active_group;
  rc = create_dbus_method_call(&set_active_group,
                               /* dest= */ BT_SERVICE_NAME,
                               /* path= */ fm->obj_path,
                               /* iface= */ BT_MEDIA_INTERFACE,
                               /* method_name= */ "GroupSetActive",
                               /* num_args= */ 1,
                               /* arg1= */ DBUS_TYPE_INT32, &dbus_group_id);

  if (rc < 0) {
    return rc;
  }

  rc = call_method_and_parse_reply(
      /* conn= */ fm->conn,
      /* method_call= */ set_active_group,
      /* dbus_ret_type= */ DBUS_TYPE_INVALID,
      /* dbus_ret_value_ptr= */ NULL,
      /* log_on_error= */ true);

  dbus_message_unref(set_active_group);

  if (rc < 0) {
    return rc;
  }

  DBusMessage* get_group_status;
  rc = create_dbus_method_call(&get_group_status,
                               /* dest= */ BT_SERVICE_NAME,
                               /* path= */ fm->obj_path,
                               /* iface= */ BT_MEDIA_INTERFACE,
                               /* method_name= */ "GetGroupStatus",
                               /* num_args= */ 1,
                               /* arg1= */ DBUS_TYPE_INT32, &dbus_group_id);

  if (rc < 0) {
    return rc;
  }

  dbus_int32_t status = -1;
  rc = retry_until_predicate_satisfied(
      /* conn=*/fm->conn,
      /* num_retries= */ LEA_AUDIO_OP_RETRIES,
      /* sleep_time_us= */ LEA_AUDIO_OP_US,
      /* method_call= */ get_group_status,
      /* dbus_ret_type= */ DBUS_TYPE_INT32,
      /* dbus_ret_value_ptr= */ &status,
      /* predicate= */ group_id == -1 ? dbus_int32_as_group_status_is_inactive
                                      : dbus_int32_as_group_status_is_active,
      /* executed_retries= */ NULL);

  dbus_message_unref(get_group_status);

  if (rc < 0) {
    syslog(LOG_WARNING,
           "%s: Failed to wait for group status to transition to %s", __func__,
           group_id == -1 ? "inactive" : "active");
  }

  return rc;
}

int floss_media_lea_set_group_volume(struct fl_media* fm,
                                     int group_id,
                                     uint8_t volume) {
  RET_IF_HAVE_FUZZER(0);

  int rc = 0;

  syslog(LOG_DEBUG, "%s(group_id=%d, volume=%u)", __func__, group_id, volume);

  if (!fm) {
    syslog(LOG_WARNING, "%s: Floss media not started", __func__);
    return -EINVAL;
  }

  dbus_int32_t dbus_group_id = group_id;

  DBusMessage* set_group_volume;
  rc = create_dbus_method_call(&set_group_volume,
                               /* dest= */ BT_SERVICE_NAME,
                               /* path= */ fm->obj_path,
                               /* iface= */ BT_MEDIA_INTERFACE,
                               /* method_name= */ "SetGroupVolume",
                               /* num_args= */ 2,
                               /* arg1= */ DBUS_TYPE_INT32, &dbus_group_id,
                               /* arg2= */ DBUS_TYPE_BYTE, &volume);

  if (rc < 0) {
    return rc;
  }

  rc = call_method_and_parse_reply(
      /* conn= */ fm->conn,
      /* method_call= */ set_group_volume,
      /* dbus_ret_type= */ DBUS_TYPE_INVALID,
      /* dbus_ret_value_ptr= */ NULL,
      /* log_on_error= */ true);

  dbus_message_unref(set_group_volume);

  return rc;
}

// When we're notified about Floss media interface is ready.
int floss_media_start(DBusConnection* conn, unsigned int hci) {
  static const DBusObjectPathVTable control_vtable = {
      .message_function = handle_bt_media_callback,
  };
  DBusError dbus_error;

  // Register the callbacks to dbus daemon.
  dbus_error_init(&dbus_error);
  if (!dbus_connection_register_object_path(conn, CRAS_BT_MEDIA_OBJECT_PATH,
                                            &control_vtable, &dbus_error)) {
    syslog(LOG_ERR, "Couldn't register CRAS control: %s: %s",
           CRAS_BT_MEDIA_OBJECT_PATH, dbus_error.message);
    dbus_error_free(&dbus_error);
    return -EIO;
  }

  // Try to be cautious if Floss media gets the state wrong.
  if (active_fm) {
    syslog(LOG_WARNING, "Floss media %s already started, overriding by hci %u",
           active_fm->obj_path, hci);
    free(active_fm);
  }

  if (fl_media_init(hci)) {
    return -ENOMEM;
  }
  active_fm->conn = conn;

  syslog(LOG_DEBUG, "floss_media_start");
  floss_media_register_callback(conn, active_fm);
  floss_media_register_telephony_callback(conn, active_fm);
  // TODO: Call config codec to Floss when we support more than just SBC.
  return 0;
}

int floss_media_stop(DBusConnection* conn, unsigned int hci) {
  if (!active_fm || active_fm->hci != hci) {
    return -ENOENT;
  }

  if (!dbus_connection_unregister_object_path(conn,
                                              CRAS_BT_MEDIA_OBJECT_PATH)) {
    syslog(LOG_WARNING, "Couldn't unregister BT media obj path");
  }

  fl_media_destroy(&active_fm);
  return 0;
}
