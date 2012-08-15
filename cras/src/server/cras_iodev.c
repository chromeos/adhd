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
#include "utlist.h"

/* Add a stream to the output (called by iodev_list).
 * Args:
 *    iodev - a pointer to the alsa_io device.
 *    stream - the new stream to add.
 * Returns:
 *    zero on success negative error otherwise.
 */
static int add_stream(struct cras_iodev *iodev,
		      struct cras_rstream *stream)
{
	struct cras_iodev_add_rm_stream_msg msg;

	assert(iodev && stream);

	msg.header.id = CRAS_IODEV_ADD_STREAM;
	msg.header.length = sizeof(struct cras_iodev_add_rm_stream_msg);
	msg.stream = stream;
	return cras_iodev_post_message_to_playback_thread(iodev, &msg.header);
}

/* Remove a stream from the output (called by iodev_list).
 * Args:
 *    iodev - a pointer to the alsa_io device.
 *    stream - the new stream to add.
 * Returns:
 *    zero on success negative error otherwise.
 */
static int rm_stream(struct cras_iodev *iodev,
		     struct cras_rstream *stream)
{
	struct cras_iodev_add_rm_stream_msg msg;

	assert(iodev && stream);

	msg.header.id = CRAS_IODEV_RM_STREAM;
	msg.header.length = sizeof(struct cras_iodev_add_rm_stream_msg);
	msg.stream = stream;
	return cras_iodev_post_message_to_playback_thread(iodev, &msg.header);
}

/*
 * Exported Interface.
 */

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
	iodev->rm_stream = rm_stream;
	iodev->add_stream = add_stream;

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
				      size_t cb_threshold,
				      size_t frame_rate,
				      struct timespec *ts)
{
	size_t to_play_usec;

	ts->tv_sec = 0;
	/* adjust sleep time to target our callback threshold */
	if (frames > cb_threshold)
		to_play_usec = (frames - cb_threshold) *
			1000000 / frame_rate;
	else
		to_play_usec = 0;
	ts->tv_nsec = to_play_usec * 1000;

	while (ts->tv_nsec > 1000000000L) {
		ts->tv_sec++;
		ts->tv_nsec -= 1000000000L;
	}
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
	ts->tv_nsec += frames * (1000000000L / frame_rate);
	while (ts->tv_nsec > 1000000000) {
		ts->tv_sec++;
		ts->tv_nsec -= 1000000000L;
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
