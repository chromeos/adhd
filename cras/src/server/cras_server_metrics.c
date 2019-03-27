/* Copyright (c) 2013 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <errno.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <syslog.h>

#include "cras_metrics.h"
#include "cras_main_message.h"
#include "cras_rstream.h"

const char kHighestDeviceDelayInput[] = "Cras.HighestDeviceDelayInput";
const char kHighestDeviceDelayOutput[] = "Cras.HighestDeviceDelayOutput";
const char kHighestInputHardwareLevel[] = "Cras.HighestInputHardwareLevel";
const char kHighestOutputHardwareLevel[] = "Cras.HighestOutputHardwareLevel";
const char kMissedCallbackFirstTimeInput[] =
		"Cras.MissedCallbackFirstTimeInput";
const char kMissedCallbackFirstTimeOutput[] =
		"Cras.MissedCallbackFirstTimeOutput";
const char kMissedCallbackFrequencyInput[] =
		"Cras.MissedCallbackFrequencyInput";
const char kMissedCallbackFrequencyOutput[] =
		"Cras.MissedCallbackFrequencyOutput";
const char kNoCodecsFoundMetric[] = "Cras.NoCodecsFoundAtBoot";
const char kStreamTimeoutMilliSeconds[] = "Cras.StreamTimeoutMilliSeconds";
const char kStreamCallbackThreshold[] = "Cras.StreamCallbackThreshold";
const char kStreamFlags[] = "Cras.StreamFlags";
const char kStreamSamplingFormat[] = "Cras.StreamSamplingFormat";
const char kStreamSamplingRate[] = "Cras.StreamSamplingRate";
const char kUnderrunsPerDevice[] = "Cras.UnderrunsPerDevice";

/*
 * Records missed callback frequency only when the runtime of stream is larger
 * than the threshold.
 */
const double MISSED_CB_FREQUENCY_SECONDS_MIN = 10.0;

/* Type of metrics to log. */
enum CRAS_SERVER_METRICS_TYPE {
	HIGHEST_DEVICE_DELAY_INPUT,
	HIGHEST_DEVICE_DELAY_OUTPUT,
	HIGHEST_INPUT_HW_LEVEL,
	HIGHEST_OUTPUT_HW_LEVEL,
	LONGEST_FETCH_DELAY,
	MISSED_CB_FIRST_TIME_INPUT,
	MISSED_CB_FIRST_TIME_OUTPUT,
	MISSED_CB_FREQUENCY_INPUT,
	MISSED_CB_FREQUENCY_OUTPUT,
	NUM_UNDERRUNS,
	STREAM_CONFIG
};

struct cras_server_metrics_stream_config {
	unsigned cb_threshold;
	unsigned flags;
	int format;
	unsigned rate;
};

union cras_server_metrics_data {
	unsigned value;
	struct cras_server_metrics_stream_config stream_config;
};

struct cras_server_metrics_message {
	struct cras_main_message header;
	enum CRAS_SERVER_METRICS_TYPE metrics_type;
	union cras_server_metrics_data data;
};

static void init_server_metrics_msg(
		struct cras_server_metrics_message *msg,
		enum CRAS_SERVER_METRICS_TYPE type,
		union cras_server_metrics_data data)
{
	memset(msg, 0, sizeof(*msg));
	msg->header.type = CRAS_MAIN_METRICS;
	msg->header.length = sizeof(*msg);
	msg->metrics_type = type;
	msg->data = data;
}

int cras_server_metrics_highest_device_delay(unsigned int hw_level,
		unsigned int largest_cb_level, enum CRAS_STREAM_DIRECTION direction)
{
	struct cras_server_metrics_message msg;
	union cras_server_metrics_data data;
	int err;

	if (largest_cb_level == 0) {
		syslog(LOG_ERR, "Failed to record device delay: devided by zero");
		return -1;
	}

	/*
	 * Because the latency depends on the callback threshold of streams, it
	 * should be calculated as dividing the highest hardware level by largest
	 * callback threshold of streams. For output device, this value should fall
	 * around 2 because CRAS 's scheduling maintain device buffer level around
	 * 1~2 minimum callback level. For input device, this value should be around
	 * 1 because the device buffer level is around 0~1 minimum callback level.
	 * Besides, UMA cannot record float so this ratio is multiplied by 1000.
	 */
	data.value = hw_level * 1000 / largest_cb_level;

	switch (direction) {
	case CRAS_STREAM_INPUT:
		init_server_metrics_msg(&msg, HIGHEST_DEVICE_DELAY_INPUT, data);
		break;
	case CRAS_STREAM_OUTPUT:
		init_server_metrics_msg(&msg, HIGHEST_DEVICE_DELAY_OUTPUT, data);
		break;
	default:
		return 0;
	}

	err = cras_main_message_send((struct cras_main_message *)&msg);
	if (err < 0) {
		syslog(LOG_ERR,
		       "Failed to send metrics message: HIGHEST_DEVICE_DELAY");
		return err;
	}

	return 0;
}

int cras_server_metrics_highest_hw_level(unsigned hw_level,
		enum CRAS_STREAM_DIRECTION direction)
{
	struct cras_server_metrics_message msg;
	union cras_server_metrics_data data;
	int err;

	data.value = hw_level;

	switch (direction) {
	case CRAS_STREAM_INPUT:
		init_server_metrics_msg(&msg, HIGHEST_INPUT_HW_LEVEL, data);
		break;
	case CRAS_STREAM_OUTPUT:
		init_server_metrics_msg(&msg, HIGHEST_OUTPUT_HW_LEVEL, data);
		break;
	default:
		return 0;
	}

	err = cras_main_message_send((struct cras_main_message *)&msg);
	if (err < 0) {
		syslog(LOG_ERR,
		       "Failed to send metrics message: HIGHEST_HW_LEVEL");
		return err;
	}

	return 0;
}

int cras_server_metrics_longest_fetch_delay(unsigned delay_msec)
{
	struct cras_server_metrics_message msg;
	union cras_server_metrics_data data;
	int err;

	data.value = delay_msec;
	init_server_metrics_msg(&msg, LONGEST_FETCH_DELAY, data);
	err = cras_main_message_send((struct cras_main_message *)&msg);
	if (err < 0) {
		syslog(LOG_ERR,
		       "Failed to send metrics message: LONGEST_FETCH_DELAY");
		return err;
	}

	return 0;
}

int cras_server_metrics_num_underruns(unsigned num_underruns)
{
	struct cras_server_metrics_message msg;
	union cras_server_metrics_data data;
	int err;

	data.value = num_underruns;
	init_server_metrics_msg(&msg, NUM_UNDERRUNS, data);
	err = cras_main_message_send((struct cras_main_message *)&msg);
	if (err < 0) {
		syslog(LOG_ERR,
		       "Failed to send metrics message: NUM_UNDERRUNS");
		return err;
	}

	return 0;
}

int cras_server_metrics_missed_cb_frequency(const struct cras_rstream *stream)
{
	struct cras_server_metrics_message msg;
	union cras_server_metrics_data data;
	struct timespec now, time_since;
	double seconds, frequency;
	int err;

	/* Do not record missed cb if the stream has these flags. */
	if (stream->flags & (BULK_AUDIO_OK | USE_DEV_TIMING | TRIGGER_ONLY))
		return 0;

	clock_gettime(CLOCK_MONOTONIC_RAW, &now);
	subtract_timespecs(&now, &stream->start_ts, &time_since);
	seconds = (double)time_since.tv_sec + time_since.tv_nsec / 1000000000.0;

	/* Ignore streams which do not have enough runtime. */
	if (seconds < MISSED_CB_FREQUENCY_SECONDS_MIN)
		return 0;

	/* Compute how many callbacks are missed in a day. */
	frequency = (double)stream->num_missed_cb * 86400.0 / seconds;
	data.value = (unsigned)(round(frequency) + 1e-9);

	if (stream->direction == CRAS_STREAM_INPUT) {
		init_server_metrics_msg(&msg, MISSED_CB_FREQUENCY_INPUT,
					data);
	} else {
		init_server_metrics_msg(&msg, MISSED_CB_FREQUENCY_OUTPUT,
					data);
	}
	err = cras_main_message_send((struct cras_main_message *)&msg);
	if (err < 0) {
		syslog(LOG_ERR,
		       "Failed to send metrics message: MISSING_CB_PER_FREQUENCY");
		return err;
	}

	return 0;
}

int cras_server_metrics_missed_cb_first_time(
		const struct cras_rstream *stream)
{
	struct cras_server_metrics_message msg;
	union cras_server_metrics_data data;
	struct timespec now, time_since;
	int err;

	/* Do not record missed cb if the stream has these flags. */
	if (stream->flags & (BULK_AUDIO_OK | USE_DEV_TIMING | TRIGGER_ONLY))
		return 0;

	clock_gettime(CLOCK_MONOTONIC_RAW, &now);
	subtract_timespecs(&now, &stream->start_ts, &time_since);
	data.value = (unsigned)time_since.tv_sec;

	if (stream->direction == CRAS_STREAM_INPUT) {
		init_server_metrics_msg(&msg, MISSED_CB_FIRST_TIME_INPUT,
					data);
	} else {
		init_server_metrics_msg(&msg, MISSED_CB_FIRST_TIME_OUTPUT,
					data);
	}
	err = cras_main_message_send((struct cras_main_message *)&msg);
	if (err < 0) {
		syslog(LOG_ERR,
		       "Failed to send metrics message: "
		       "MISSING_CB_FIRST_TIME");
		return err;
	}

	return 0;
}

int cras_server_metrics_stream_config(struct cras_rstream_config *config)
{
	struct cras_server_metrics_message msg;
	union cras_server_metrics_data data;
	int err;

	data.stream_config.cb_threshold = (unsigned)config->cb_threshold;
	data.stream_config.flags = (unsigned)config->flags;
	data.stream_config.format = (int)config->format->format;
	data.stream_config.rate = (unsigned)config->format->frame_rate;

	init_server_metrics_msg(&msg, STREAM_CONFIG, data);
	err = cras_main_message_send((struct cras_main_message *)&msg);
	if (err < 0) {
		syslog(LOG_ERR,
			"Failed to send metrics message: STREAM_CONFIG");
		return err;
	}

	return 0;
}

static void metrics_stream_config(
		struct cras_server_metrics_stream_config config)
{
	/* Logs stream callback threshold. */
	cras_metrics_log_sparse_histogram(kStreamCallbackThreshold,
					  config.cb_threshold);

	/* Logs stream flags. */
	cras_metrics_log_sparse_histogram(kStreamFlags,
					  config.flags);

	/* Logs stream sampling format. */
	cras_metrics_log_sparse_histogram(kStreamSamplingFormat,
					  config.format);

	/* Logs stream sampling rate. */
	cras_metrics_log_sparse_histogram(kStreamSamplingRate,
					  config.rate);
}

static void handle_metrics_message(struct cras_main_message *msg, void *arg)
{
	struct cras_server_metrics_message *metrics_msg =
			(struct cras_server_metrics_message *)msg;
	switch (metrics_msg->metrics_type) {
	case HIGHEST_DEVICE_DELAY_INPUT:
		cras_metrics_log_histogram(kHighestDeviceDelayInput,
				metrics_msg->data.value, 1, 10000, 20);
		break;
	case HIGHEST_DEVICE_DELAY_OUTPUT:
		cras_metrics_log_histogram(kHighestDeviceDelayOutput,
				metrics_msg->data.value, 1, 10000, 20);
		break;
	case HIGHEST_INPUT_HW_LEVEL:
		cras_metrics_log_histogram(kHighestInputHardwareLevel,
				metrics_msg->data.value, 1, 10000, 20);
		break;
	case HIGHEST_OUTPUT_HW_LEVEL:
		cras_metrics_log_histogram(kHighestOutputHardwareLevel,
				metrics_msg->data.value, 1, 10000, 20);
		break;
	case LONGEST_FETCH_DELAY:
		cras_metrics_log_histogram(kStreamTimeoutMilliSeconds,
				metrics_msg->data.value, 1, 20000, 10);
		break;
	case MISSED_CB_FIRST_TIME_INPUT:
		cras_metrics_log_histogram(kMissedCallbackFirstTimeInput,
				metrics_msg->data.value, 0, 90000, 20);
		break;
	case MISSED_CB_FIRST_TIME_OUTPUT:
		cras_metrics_log_histogram(kMissedCallbackFirstTimeOutput,
				metrics_msg->data.value, 0, 90000, 20);
		break;
	case MISSED_CB_FREQUENCY_INPUT:
		cras_metrics_log_histogram(kMissedCallbackFrequencyInput,
				metrics_msg->data.value, 0, 90000, 20);
		break;
	case MISSED_CB_FREQUENCY_OUTPUT:
		cras_metrics_log_histogram(kMissedCallbackFrequencyOutput,
				metrics_msg->data.value, 0, 90000, 20);
		break;
	case NUM_UNDERRUNS:
		cras_metrics_log_histogram(kUnderrunsPerDevice,
				metrics_msg->data.value, 0, 1000, 10);
		break;
	case STREAM_CONFIG:
		metrics_stream_config(metrics_msg->data.stream_config);
		break;
	default:
		syslog(LOG_ERR, "Unknown metrics type %u",
		       metrics_msg->metrics_type);
		break;
	}

}

int cras_server_metrics_init() {
	cras_main_message_add_handler(CRAS_MAIN_METRICS,
				      handle_metrics_message, NULL);
	return 0;
}
