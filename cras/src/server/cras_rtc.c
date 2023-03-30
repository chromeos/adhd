/* Copyright 2021 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
#include <syslog.h>

#if CRAS_DBUS
#include "cras/src/server/cras_dbus_control.h"
#endif

#include "cras/src/common/cras_string.h"
#include "cras/src/server/cras_iodev.h"
#include "cras/src/server/cras_rstream.h"
#include "cras/src/server/cras_rtc.h"
#include "cras/src/server/cras_server_metrics.h"
#include "cras_util.h"
#include "third_party/utlist/utlist.h"

struct rtc_data {
  struct cras_rstream* stream;
  struct cras_iodev* iodev;
  struct timespec start_ts;
  struct rtc_data *prev, *next;
};
struct rtc_data* input_list = NULL;
struct rtc_data* output_list = NULL;

static bool check_rtc_stream(struct cras_rstream* stream, unsigned int dev_id) {
  return cras_rtc_check_stream_config(stream) &&
         dev_id >= MAX_SPECIAL_DEVICE_IDX;
}

static void set_all_rtc_streams(struct rtc_data* list) {
  struct rtc_data* data;
  DL_FOREACH (list, data) {
    data->stream->stream_type = CRAS_STREAM_TYPE_VOICE_COMMUNICATION;
  }
}

static struct rtc_data* find_rtc_stream(struct rtc_data* list,
                                        struct cras_rstream* stream,
                                        unsigned int dev_id) {
  struct rtc_data* data;

  DL_FOREACH (list, data) {
    if (data->stream == stream && data->iodev->info.idx == dev_id) {
      return data;
    }
  }
  syslog(LOG_WARNING, "Could not find rtc stream %x", stream->stream_id);
  return NULL;
}

static void notify_rtc_active_now(bool was_active) {
#if CRAS_DBUS
  bool now_active = cras_rtc_is_running();

  if (now_active != was_active) {
    cras_dbus_notify_rtc_active(now_active);
  }
#endif
}

bool cras_rtc_check_stream_config(const struct cras_rstream* stream) {
  return stream->cb_threshold == 480 &&
         (stream->client_type == CRAS_CLIENT_TYPE_CHROME ||
          stream->client_type == CRAS_CLIENT_TYPE_LACROS ||
          stream->client_type == CRAS_CLIENT_TYPE_TEST);
}
/*
 * Detects whether there is a RTC stream pair based on these rules:
 * 1. The cb_threshold is 480.
 * 2. There are two streams whose directions are opposite.
 * 3. Two streams are from Chrome or LaCrOS.
 * If all rules are passed, set the stream type to the voice communication.
 */
void cras_rtc_add_stream(struct cras_rstream* stream,
                         struct cras_iodev* iodev) {
  struct rtc_data* data;
  bool rtc_active_before = cras_rtc_is_running();

  if (!check_rtc_stream(stream, iodev->info.idx)) {
    return;
  }

  data = (struct rtc_data*)calloc(1, sizeof(struct rtc_data));
  if (!data) {
    syslog(LOG_ERR, "Failed to calloc: %s", cras_strerror(errno));
    return;
  }
  data->stream = stream;
  data->iodev = iodev;
  clock_gettime(CLOCK_MONOTONIC_RAW, &data->start_ts);
  if (stream->direction == CRAS_STREAM_INPUT) {
    if (output_list) {
      stream->stream_type = CRAS_STREAM_TYPE_VOICE_COMMUNICATION;
    }
    if (!input_list && output_list) {
      set_all_rtc_streams(output_list);
    }
    DL_APPEND(input_list, data);
  } else {
    if (input_list) {
      stream->stream_type = CRAS_STREAM_TYPE_VOICE_COMMUNICATION;
    }
    if (!output_list && input_list) {
      set_all_rtc_streams(input_list);
    }
    DL_APPEND(output_list, data);
  }

  notify_rtc_active_now(rtc_active_before);
}

/*
 * Remove the stream from the RTC stream list.
 */
void cras_rtc_remove_stream(struct cras_rstream* stream, unsigned int dev_id) {
  struct rtc_data* data;
  struct rtc_data* tmp;
  struct timespec* start_ts;
  bool rtc_active_before = cras_rtc_is_running();

  if (!check_rtc_stream(stream, dev_id)) {
    return;
  }

  if (stream->direction == CRAS_STREAM_INPUT) {
    data = find_rtc_stream(input_list, stream, dev_id);
    if (!data) {
      return;
    }
    DL_DELETE(input_list, data);
    DL_FOREACH (output_list, tmp) {
      start_ts = timespec_after(&data->start_ts, &tmp->start_ts)
                     ? &data->start_ts
                     : &tmp->start_ts;
      cras_server_metrics_webrtc_devs_runtime(data->iodev, tmp->iodev,
                                              start_ts);
    }
  } else {
    data = find_rtc_stream(output_list, stream, dev_id);
    if (!data) {
      return;
    }
    DL_DELETE(output_list, data);
    DL_FOREACH (input_list, tmp) {
      start_ts = timespec_after(&data->start_ts, &tmp->start_ts)
                     ? &data->start_ts
                     : &tmp->start_ts;
      cras_server_metrics_webrtc_devs_runtime(tmp->iodev, data->iodev,
                                              start_ts);
    }
  }
  free(data);

  notify_rtc_active_now(rtc_active_before);
}

bool cras_rtc_is_running() {
  return input_list && output_list;
}
