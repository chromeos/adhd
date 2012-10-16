/* Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <pthread.h>
#include <stdlib.h>
#include <sys/time.h>
#include <syslog.h>
#include <time.h>

#include "cras_iodev.h"
#include "cras_rstream.h"
#include "cras_system_state.h"
#include "utlist.h"

/*
 * Exported Interface.
 */

int cras_iodev_add_stream(struct cras_iodev *iodev,
			  struct cras_rstream *stream)
{
	struct cras_iodev_add_rm_stream_msg msg;

	assert(iodev && stream);

	msg.header.id = CRAS_IODEV_ADD_STREAM;
	msg.header.length = sizeof(struct cras_iodev_add_rm_stream_msg);
	msg.stream = stream;
	return cras_iodev_post_message_to_playback_thread(iodev, &msg.header);
}

int cras_iodev_rm_stream(struct cras_iodev *iodev,
			 struct cras_rstream *stream)
{
	struct cras_iodev_add_rm_stream_msg msg;

	assert(iodev && stream);

	msg.header.id = CRAS_IODEV_RM_STREAM;
	msg.header.length = sizeof(struct cras_iodev_add_rm_stream_msg);
	msg.stream = stream;
	return cras_iodev_post_message_to_playback_thread(iodev, &msg.header);
}

int cras_iodev_init(struct cras_iodev *iodev,
		    enum CRAS_STREAM_DIRECTION direction,
		    void *(*thread_function)(void *arg),
		    void *thread_data)
{
	int rc;

	iodev->to_thread_fds[0] = -1;
	iodev->to_thread_fds[1] = -1;
	iodev->to_main_fds[0] = -1;
	iodev->to_main_fds[1] = -1;
	iodev->direction = direction;

	/* Two way pipes for communication with the device's audio thread. */
	rc = pipe(iodev->to_thread_fds);
	if (rc < 0) {
		syslog(LOG_ERR, "Failed to pipe");
		return rc;
	}
	rc = pipe(iodev->to_main_fds);
	if (rc < 0) {
		syslog(LOG_ERR, "Failed to pipe");
		return rc;
	}

	/* Start the device thread, it will block until a stream is added. */
	rc = pthread_create(&iodev->tid, NULL, thread_function, thread_data);
	if (rc != 0) {
		syslog(LOG_ERR, "Failed pthread_create");
		return rc;
	}

	return 0;
}

/* Finds the supported sample rate that best suits the requested rate, "rrate".
 * Exact matches have highest priority, then integer multiples, then the default
 * rate for the device. */
static size_t get_best_rate(struct cras_iodev *iodev, size_t rrate)
{
	size_t i;
	size_t best;

	if (iodev->supported_rates[0] == 0) /* No rates supported */
		return 0;

	for (i = 0, best = 0; iodev->supported_rates[i] != 0; i++) {
		if (rrate == iodev->supported_rates[i])
			return rrate;
		if (best == 0 && (rrate % iodev->supported_rates[i] == 0 ||
				  iodev->supported_rates[i] % rrate == 0))
			best = iodev->supported_rates[i];
	}

	if (best)
		return best;
	return iodev->supported_rates[0];
}

/* Finds the best match for the channel count.  This will return an exact match
 * only, if there is no exact match, it falls back to the default channel count
 * for the device (The first in the list). */
static size_t get_best_channel_count(struct cras_iodev *iodev, size_t count)
{
	size_t i;

	assert(iodev->supported_channel_counts[0] != 0);

	for (i = 0; iodev->supported_channel_counts[i] != 0; i++) {
		if (iodev->supported_channel_counts[i] == count)
			return count;
	}
	return iodev->supported_channel_counts[0];
}

void cras_iodev_deinit(struct cras_iodev *iodev)
{
	struct cras_iodev_msg msg;

	msg.id = CRAS_IODEV_STOP;
	msg.length = sizeof(msg);
	cras_iodev_post_message_to_playback_thread(iodev, &msg);
	pthread_join(iodev->tid, NULL);

	if (iodev->to_thread_fds[0] != -1) {
		close(iodev->to_thread_fds[0]);
		close(iodev->to_thread_fds[1]);
		iodev->to_thread_fds[0] = -1;
		iodev->to_thread_fds[1] = -1;
	}
	if (iodev->to_main_fds[0] != -1) {
		close(iodev->to_main_fds[0]);
		close(iodev->to_main_fds[1]);
		iodev->to_main_fds[0] = -1;
		iodev->to_main_fds[1] = -1;
	}

	cras_iodev_free_format(iodev);
}

int cras_iodev_set_format(struct cras_iodev *iodev,
			  struct cras_audio_format *fmt)
{
	size_t actual_rate, actual_num_channels;
	const char *purpose;

	/* If this device isn't already using a format, try to match the one
	 * requested in "fmt". */
	if (iodev->format == NULL) {
		iodev->format = malloc(sizeof(struct cras_audio_format));
		if (!iodev->format)
			return -ENOMEM;
		*iodev->format = *fmt;

		if (iodev->update_supported_formats) {
			int rc = iodev->update_supported_formats(iodev);
			if (rc) {
				syslog(LOG_ERR, "Failed to update formats");
				return rc;
			}
		}


		actual_rate = get_best_rate(iodev, fmt->frame_rate);
		actual_num_channels = get_best_channel_count(iodev,
							     fmt->num_channels);
		if (actual_rate == 0 || actual_num_channels == 0) {
			/* No compatible frame rate found. */
			free(iodev->format);
			iodev->format = NULL;
			return -EINVAL;
		}
		iodev->format->frame_rate = actual_rate;
		iodev->format->num_channels = actual_num_channels;
		/* TODO(dgreid) - allow other formats. */
		iodev->format->format = SND_PCM_FORMAT_S16_LE;

		if (iodev->direction == CRAS_STREAM_OUTPUT)
			purpose = "playback";
		else
			purpose = "capture";
		iodev->dsp_context = cras_dsp_context_new(actual_num_channels,
							  actual_rate, purpose);
		if (iodev->dsp_context)
			cras_dsp_load_pipeline(iodev->dsp_context);
	}

	*fmt = *(iodev->format);
	return 0;
}

void cras_iodev_free_format(struct cras_iodev *iodev)
{
	if (iodev->format) {
		free(iodev->format);
		iodev->format = NULL;
	}
	if (iodev->dsp_context) {
		cras_dsp_context_free(iodev->dsp_context);
		iodev->dsp_context = NULL;
	}
}

int cras_iodev_append_stream(struct cras_iodev *iodev,
			     struct cras_rstream *stream)
{
	struct cras_io_stream *out;

	/* Check that we don't already have this stream */
	DL_SEARCH_SCALAR(iodev->streams, out, stream, stream);
	if (out != NULL)
		return -EEXIST;

	/* New stream, allocate a container and add it to the list. */
	out = calloc(1, sizeof(*out));
	if (out == NULL)
		return -ENOMEM;
	out->stream = stream;
	out->shm = cras_rstream_get_shm(stream);
	out->fd = cras_rstream_get_audio_fd(stream);
	DL_APPEND(iodev->streams, out);

	cras_system_state_stream_added();

	return 0;
}

int cras_iodev_delete_stream(struct cras_iodev *iodev,
			     struct cras_rstream *stream)
{
	struct cras_io_stream *out;

	/* Find stream, and if found, delete it. */
	DL_SEARCH_SCALAR(iodev->streams, out, stream, stream);
	if (out == NULL)
		return -EINVAL;
	DL_DELETE(iodev->streams, out);
	free(out);

	cras_system_state_stream_removed();

	return 0;
}

int cras_iodev_post_message_to_playback_thread(struct cras_iodev *iodev,
					       struct cras_iodev_msg *msg)
{
	int rc, err;

	err = write(iodev->to_thread_fds[1], msg, msg->length);
	if (err < 0) {
		syslog(LOG_ERR,
		       "Failed to post message to thread for iodev %zu.",
		       iodev->info.idx);
		return err;
	}
	/* Synchronous action, wait for response. */
	err = read(iodev->to_main_fds[0], &rc, sizeof(rc));
	if (err < 0) {
		syslog(LOG_ERR,
		       "Failed to read reply from thread for iodev %zu.",
		       iodev->info.idx);
		return err;
	}

	return rc;
}

void cras_iodev_fill_time_from_frames(size_t frames,
				      size_t frame_rate,
				      struct timespec *ts)
{
	uint64_t to_play_usec;

	ts->tv_sec = 0;
	/* adjust sleep time to target our callback threshold */
	to_play_usec = (uint64_t)frames * 1000000L / (uint64_t)frame_rate;

	while (to_play_usec > 1000000) {
		ts->tv_sec++;
		to_play_usec -= 1000000;
	}
	ts->tv_nsec = to_play_usec * 1000;
}

int cras_iodev_send_command_response(struct cras_iodev *iodev, int rc)
{
	return write(iodev->to_main_fds[1], &rc, sizeof(rc));
}

int cras_iodev_read_thread_command(struct cras_iodev *iodev,
				   uint8_t *buf,
				   size_t max_len)
{
	int to_read, nread, rc;
	struct cras_iodev_msg *msg = (struct cras_iodev_msg *)buf;

	/* Get the length of the message first */
	nread = read(iodev->to_thread_fds[0], buf, sizeof(msg->length));
	if (nread < 0)
		return nread;
	if (msg->length > max_len)
		return -ENOMEM;

	to_read = msg->length - nread;
	rc = read(iodev->to_thread_fds[0], &buf[0] + nread, to_read);
	if (rc < 0)
		return rc;
	return 0;
}

int cras_iodev_get_thread_poll_fd(const struct cras_iodev *iodev)
{
	return iodev->to_thread_fds[0];
}

void cras_iodev_set_playback_timestamp(size_t frame_rate,
				       size_t frames,
				       struct timespec *ts)
{
	clock_gettime(CLOCK_MONOTONIC, ts);

	/* For playback, want now + samples left to be played.
	 * ts = time next written sample will be played to DAC,
	 */
	ts->tv_nsec += frames * 1000000000ULL / frame_rate;
	while (ts->tv_nsec > 1000000000ULL) {
		ts->tv_sec++;
		ts->tv_nsec -= 1000000000ULL;
	}
}

void cras_iodev_set_capture_timestamp(size_t frame_rate,
				      size_t frames,
				      struct timespec *ts)
{
	long tmp;

	clock_gettime(CLOCK_MONOTONIC, ts);

	/* For capture, now - samples left to be read.
	 * ts = time next sample to be read was captured at ADC.
	 */
	tmp = frames * (1000000000L / frame_rate);
	while (tmp > 1000000000L) {
		tmp -= 1000000000L;
		ts->tv_sec--;
	}
	if (ts->tv_nsec >= tmp)
		ts->tv_nsec -= tmp;
	else {
		tmp -= ts->tv_nsec;
		ts->tv_nsec = 1000000000L - tmp;
		ts->tv_sec--;
	}
}

void cras_iodev_config_params_for_streams(struct cras_iodev *iodev)
{
	const struct cras_io_stream *lowest, *curr;
	/* Base settings on the lowest-latency stream. */
	lowest = iodev->streams;
	DL_FOREACH(iodev->streams, curr)
		if (cras_rstream_get_buffer_size(curr->stream) <
		    cras_rstream_get_buffer_size(lowest->stream))
			lowest = curr;

	iodev->used_size = cras_rstream_get_buffer_size(lowest->stream);
	if (iodev->used_size > iodev->buffer_size)
		iodev->used_size = iodev->buffer_size;
	iodev->cb_threshold = cras_rstream_get_cb_threshold(lowest->stream);

	/* For output streams, callback when at most half way full. */
	if (iodev->direction == CRAS_STREAM_OUTPUT &&
	    iodev->cb_threshold > iodev->used_size / 2)
		iodev->cb_threshold = iodev->used_size / 2;
}

void cras_iodev_plug_event(struct cras_iodev *iodev, int plugged)
{
	if (plugged)
		gettimeofday(&iodev->info.plugged_time, NULL);
	iodev->info.plugged = plugged;
}
