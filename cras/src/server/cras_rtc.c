/* Copyright 2021 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
#include <syslog.h>

#include "cras_iodev.h"
#include "cras_rtc.h"
#include "cras_rstream.h"
#include "cras_util.h"
#include "utlist.h"

struct rtc_data {
	struct cras_rstream *stream;
	struct cras_iodev *iodev;
	struct timespec start_ts;
	struct rtc_data *prev, *next;
};
struct rtc_data *input_list = NULL;
struct rtc_data *output_list = NULL;

static bool check_rtc_stream(struct cras_rstream *stream, unsigned int dev_id)
{
	return stream->cb_threshold == 480 &&
	       (stream->client_type == CRAS_CLIENT_TYPE_CHROME ||
		stream->client_type == CRAS_CLIENT_TYPE_LACROS) &&
	       dev_id >= MAX_SPECIAL_DEVICE_IDX;
}

static void set_all_rtc_streams(struct rtc_data *list)
{
	struct rtc_data *data;
	DL_FOREACH (list, data) {
		data->stream->stream_type =
			CRAS_STREAM_TYPE_VOICE_COMMUNICATION;
	}
}

static struct rtc_data *find_rtc_stream(struct rtc_data *list,
					struct cras_rstream *stream,
					unsigned int dev_id)
{
	struct rtc_data *data;

	DL_FOREACH (list, data) {
		if (data->stream == stream && data->iodev->info.idx == dev_id)
			return data;
	}
	syslog(LOG_ERR, "Could not find rtc stream %x", stream->stream_id);
	return NULL;
}

/*
 * Detects whether there is a RTC stream pair based on these rules:
 * 1. The cb_threshold is 480.
 * 2. There are two streams whose directions are opposite.
 * 3. Two streams are from Chrome or LaCrOS.
 * If all rules are passed, set the stream type to the voice communication.
 */
void cras_rtc_add_stream(struct cras_rstream *stream, struct cras_iodev *iodev)
{
	struct rtc_data *data;

	if (!check_rtc_stream(stream, iodev->info.idx))
		return;

	data = (struct rtc_data *)calloc(1, sizeof(struct rtc_data));
	if (!data) {
		syslog(LOG_ERR, "Failed to calloc: %s", strerror(errno));
		return;
	}
	data->stream = stream;
	data->iodev = iodev;
	clock_gettime(CLOCK_MONOTONIC_RAW, &data->start_ts);
	if (stream->direction == CRAS_STREAM_INPUT) {
		if (output_list)
			stream->stream_type =
				CRAS_STREAM_TYPE_VOICE_COMMUNICATION;
		if (!input_list && output_list)
			set_all_rtc_streams(output_list);
		DL_APPEND(input_list, data);
	} else {
		if (input_list)
			stream->stream_type =
				CRAS_STREAM_TYPE_VOICE_COMMUNICATION;
		if (!output_list && input_list)
			set_all_rtc_streams(input_list);
		DL_APPEND(output_list, data);
	}
}

/*
 * Remove the stream from the RTC stream list.
 */
void cras_rtc_remove_stream(struct cras_rstream *stream, unsigned int dev_id)
{
	struct rtc_data *data;

	if (!check_rtc_stream(stream, dev_id))
		return;

	if (stream->direction == CRAS_STREAM_INPUT) {
		data = find_rtc_stream(input_list, stream, dev_id);
		if (!data)
			return;
		DL_DELETE(input_list, data);
	} else {
		data = find_rtc_stream(output_list, stream, dev_id);
		if (!data)
			return;
		DL_DELETE(output_list, data);
	}
	free(data);
}
