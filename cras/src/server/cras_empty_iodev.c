/* Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <pthread.h>
#include <syslog.h>

#include "cras_config.h"
#include "cras_iodev.h"
#include "cras_iodev_list.h"
#include "cras_rstream.h"
#include "cras_types.h"
#include "utlist.h"

static const size_t EMPTY_IODEV_PRIORITY = 0; /* lowest possible */
static const unsigned int EMPTY_BUFFER_SIZE  = 48 * 1024;

static size_t empty_supported_rates[] = {
	44100, 48000, 0
};

static size_t empty_supported_channel_counts[] = {
	1, 2, 0
};

/* Handles the add_stream message from the main thread. */
static int thread_add_stream(struct cras_iodev *iodev,
			     struct cras_rstream *stream)
{
	int rc;
	const int first_stream = iodev->streams == NULL;

	/* Only allow one capture stream to attach. */
	if (iodev->direction == CRAS_STREAM_INPUT &&
	    !first_stream)
		return -EBUSY;

	rc = cras_iodev_append_stream(iodev, stream);
	if (rc < 0)
		return rc;
	cras_iodev_config_params_for_streams(iodev);

	if (first_stream) {
		if (iodev->format == NULL) {
			cras_iodev_delete_stream(iodev, stream);
			return -EINVAL;
		}
		cras_rstream_get_format(stream, iodev->format);
	}
	return 0;
}

/* Handles the rm_stream message from the main thread.
 * If this is the last stream to be removed then stop the
 * audio thread and free the resources. */
static int thread_remove_stream(struct cras_iodev *iodev,
				struct cras_rstream *stream)
{
	int rc;

	rc = cras_iodev_delete_stream(iodev, stream);
	if (rc != 0)
		return rc;

	if (!cras_iodev_streams_attached(iodev)) {
		cras_iodev_free_format(iodev);
	} else {
		cras_iodev_config_params_for_streams(iodev);
		syslog(LOG_DEBUG,
		       "used_size %u format %u cb_threshold %u",
		       (unsigned)iodev->used_size,
		       iodev->format->format,
		       (unsigned)iodev->cb_threshold);
	}
	cras_rstream_set_iodev(stream, NULL);
	return 0;
}

/* Handle a message sent to the playback thread */
static int handle_playback_thread_message(struct cras_iodev *iodev)
{
	uint8_t buf[256];
	struct cras_iodev_msg *msg = (struct cras_iodev_msg *)buf;
	int ret = 0;
	int err;

	err = cras_iodev_read_thread_command(iodev, buf, 256);
	if (err < 0)
		return err;

	switch (msg->id) {
	case CRAS_IODEV_ADD_STREAM: {
		struct cras_iodev_add_rm_stream_msg *amsg;
		amsg = (struct cras_iodev_add_rm_stream_msg *)msg;
		ret = thread_add_stream(iodev, amsg->stream);
		break;
	}
	case CRAS_IODEV_RM_STREAM: {
		struct cras_iodev_add_rm_stream_msg *rmsg;
		rmsg = (struct cras_iodev_add_rm_stream_msg *)msg;
		ret = thread_remove_stream(iodev, rmsg->stream);
		break;
	}
	case CRAS_IODEV_STOP:
		ret = 0;
		err = cras_iodev_send_command_response(iodev, ret);
		if (err < 0)
			return err;
		pthread_exit(0);
		break;
	default:
		syslog(LOG_WARNING, "Unknown command %d %d.", err, msg->id);
		ret = -EINVAL;
		break;
	}

	err = cras_iodev_send_command_response(iodev, ret);
	if (err < 0)
		return err;
	return ret;
}

/* Fills an input stream with zeros. */
static void fill_capture(struct cras_iodev *iodev, struct timespec *ts)
{
	uint8_t *dst;
	unsigned count;
	int rc;

	ts->tv_sec = 0;
	ts->tv_nsec = 0;

	dst = cras_shm_get_writeable_frames(iodev->streams->shm, &count);
	count = min(count, iodev->cb_threshold);
	memset(dst, 0, count * cras_shm_frame_bytes(iodev->streams->shm));
	cras_shm_check_write_overrun(iodev->streams->shm);
	cras_shm_buffer_written(iodev->streams->shm, count);
	cras_shm_buffer_write_complete(iodev->streams->shm);

	cras_iodev_set_capture_timestamp(iodev->format->frame_rate,
					 count,
					 &iodev->streams->shm->area->ts);

	rc = cras_rstream_audio_ready(iodev->streams->stream, count);
	if (rc < 0) {
		thread_remove_stream(iodev, iodev->streams->stream);
		return;
	}

	ts->tv_nsec = count * 1000000 / iodev->format->frame_rate;
	ts->tv_nsec *= 1000;
}

/* Reads any pending audio message from the socket and throws them away. */
static void flush_old_aud_messages(struct cras_audio_shm *shm, int fd)
{
	struct audio_message msg;
	struct timespec ts = {0, 0};
	fd_set poll_set;
	int err;

	do {
		FD_ZERO(&poll_set);
		FD_SET(fd, &poll_set);
		err = pselect(fd + 1, &poll_set, NULL, NULL, &ts, NULL);
		if (err > 0 && FD_ISSET(fd, &poll_set)) {
			err = read(fd, &msg, sizeof(msg));
			cras_shm_set_callback_pending(shm, 0);
		}
	} while (err > 0);
}

/* Asks any streams with room for more data. Sets the timestamp for all streams.
 * If a stream has an error (broken pipe), remove it.  If there are still
 * streams, return success to keep going for the remaining streams.  Only return
 * an error if all streams have been removed.  In that case we want to signal
 * that there is no need to go on and look for audio (because there are no
 * streams left to get it from).
 * Args:
 *    iodev - The iodev containing the streams to fetch from.
 *    fetch_size - How much to fetch in frames.
 * Returns:
 *    0 on success, negative error on failure. If failed, can assume that all
 *    streams have been removed from the device.
 */
static int fetch_and_set_timestamp(struct cras_iodev *iodev, size_t fetch_size)
{
	size_t fr_rate, frames_in_buff;
	struct cras_io_stream *curr, *tmp;
	int rc;

	fr_rate = iodev->format->frame_rate;

	DL_FOREACH_SAFE(iodev->streams, curr, tmp) {
		if (cras_shm_callback_pending(curr->shm))
			flush_old_aud_messages(curr->shm, curr->fd);

		frames_in_buff = cras_shm_get_frames(curr->shm);

		cras_iodev_set_playback_timestamp(fr_rate,
						  frames_in_buff,
						  &curr->shm->area->ts);

		/* If we already have enough data, don't poll this stream. */
		if (frames_in_buff >= fetch_size)
			continue;

		if (!cras_shm_callback_pending(curr->shm) &&
		    cras_shm_is_buffer_available(curr->shm)) {
			rc = cras_rstream_request_audio(curr->stream,
							fetch_size);
			if (rc < 0) {
				thread_remove_stream(iodev, curr->stream);
				/* If this failed and was the last stream,
				 * return, otherwise, on to the next one */
				if (!cras_iodev_streams_attached(iodev))
					return -EIO;
				continue;
			}
			cras_shm_set_callback_pending(curr->shm, 1);
		}
	}

	return 0;
}

/* Throw away any samples received from the stream. */
static void dump_playback(struct cras_iodev *iodev, struct timespec *ts)
{
	struct cras_io_stream *curr, *tmp;
	struct timeval to;
	fd_set poll_set, this_set;
	size_t streams_wait, num_mixed;
	int max_fd;
	int rc;
	size_t write_limit;

	/* Timeout on reading before we under-run. Leaving time to mix. */
	to.tv_sec = 0;
	to.tv_usec = (iodev->cb_threshold * 1000000 /
		      iodev->format->frame_rate);

	write_limit = iodev->used_size - iodev->cb_threshold;
	rc = fetch_and_set_timestamp(iodev, write_limit);
	if (rc < 0)
		return;

	FD_ZERO(&poll_set);
	max_fd = -1;
	streams_wait = 0;
	num_mixed = 0;

	/* Check if streams have enough data to fill this request,
	 * if not, wait for them. Mix all streams we have enough data for. */
	DL_FOREACH(iodev->streams, curr) {
		curr->mixed = 0;
		if (cras_shm_get_frames(curr->shm) < write_limit &&
		    cras_shm_callback_pending(curr->shm)) {
			/* Not enough to mix this call, wait for a response. */
			streams_wait++;
			FD_SET(curr->fd, &poll_set);
			if (curr->fd > max_fd)
				max_fd = curr->fd;
		} else {
			curr->mixed = cras_shm_get_frames(curr->shm);
			if (curr->mixed > 0) {
				write_limit = curr->mixed;
				num_mixed++;
			}
		}
	}

	/* Wait until all polled clients reply, or a timeout. */
	while (streams_wait > 0) {
		this_set = poll_set;
		rc = select(max_fd + 1, &this_set, NULL, NULL, &to);
		if (rc <= 0) {
			/* Timeout */
			DL_FOREACH(iodev->streams, curr) {
				if (cras_shm_callback_pending(curr->shm) &&
				    FD_ISSET(curr->fd, &poll_set))
					cras_shm_inc_cb_timeouts(curr->shm);
			}
			break;
		}
		DL_FOREACH_SAFE(iodev->streams, curr, tmp) {
			if (!FD_ISSET(curr->fd, &this_set))
				continue;

			FD_CLR(curr->fd, &poll_set);
			streams_wait--;
			cras_shm_set_callback_pending(curr->shm, 0);
			rc = cras_rstream_get_audio_request_reply(curr->stream);
			if (rc < 0) {
				thread_remove_stream(iodev, curr->stream);
				if (!cras_iodev_streams_attached(iodev))
					return;
				continue;
			}
			if (curr->mixed)
				continue;
			curr->mixed = cras_shm_get_frames(curr->shm);
			if (curr->mixed > 0) {
				write_limit = curr->mixed;
				num_mixed++;
			}
		}
	}

	/* For all streams rendered, mark the data used as read. */
	DL_FOREACH(iodev->streams, curr)
		if (curr->mixed)
			cras_shm_buffer_read(curr->shm, write_limit);

	/* Set the sleep time based on how much is left to play */
	cras_iodev_fill_time_from_frames(write_limit,
					 iodev->format->frame_rate,
					 ts);
}

/* For playback, fill the audio buffer when needed, for capture, pull out
 * samples when they are ready.  This thread will attempt to run at a high
 * priority to allow for low latency streams.  Samples played to this thread are
 * thrown away, and zeros are returned for capture..  It can also be woken by
 * sending it a message using the "cras_iodev_post_message_to_playback_thread"
 * function.
 */
static void *empty_io_thread(void *arg)
{
	struct cras_iodev *iodev = (struct cras_iodev *)arg;
	struct timespec ts;
	fd_set poll_set;
	int msg_fd;
	int err;

	msg_fd = cras_iodev_get_thread_poll_fd(iodev);

	/* Attempt to get realtime scheduling */
	if (cras_set_rt_scheduling(CRAS_SERVER_RT_THREAD_PRIORITY) == 0)
		cras_set_thread_priority(CRAS_SERVER_RT_THREAD_PRIORITY);

	while (1) {
		struct timespec *wait_ts;

		wait_ts = NULL;

		if (cras_iodev_streams_attached(iodev)) {
			if (iodev->direction == CRAS_STREAM_INPUT)
				fill_capture(iodev, &ts);
			else
				dump_playback(iodev, &ts);
			wait_ts = &ts;
		}

		FD_ZERO(&poll_set);
		FD_SET(msg_fd, &poll_set);
		err = pselect(msg_fd + 1, &poll_set, NULL, NULL, wait_ts, NULL);
		if (err > 0 && FD_ISSET(msg_fd, &poll_set)) {
			err = handle_playback_thread_message(iodev);
			if (err < 0)
				syslog(LOG_INFO, "handle message %d", err);
		}
	}

	return NULL;
}

/*
 * Exported Interface.
 */

struct cras_iodev *empty_iodev_create(enum CRAS_STREAM_DIRECTION direction)
{
	struct cras_iodev *iodev;
	iodev = calloc(1, sizeof(*iodev));
	if (iodev == NULL)
		return NULL;
	iodev->direction = direction;

	if (cras_iodev_init(iodev, empty_io_thread, iodev)) {
		syslog(LOG_ERR, "Failed to create empty iodev.");
		cras_iodev_deinit(iodev);
		free(iodev);
		return NULL;
	}

	iodev->info.priority = EMPTY_IODEV_PRIORITY;

	/* Finally add it to the appropriate iodev list. */
	if (direction == CRAS_STREAM_INPUT) {
		snprintf(iodev->info.name,
			 ARRAY_SIZE(iodev->info.name),
			 "Silent record device.");
		iodev->info.name[ARRAY_SIZE(iodev->info.name) - 1] = '\0';
		cras_iodev_list_add_input(iodev);
	} else {
		assert(direction == CRAS_STREAM_OUTPUT);
		snprintf(iodev->info.name,
			 ARRAY_SIZE(iodev->info.name),
			 "Silent playback device.");
		iodev->info.name[ARRAY_SIZE(iodev->info.name) - 1] = '\0';
		cras_iodev_list_add_output(iodev);
	}

	iodev->supported_rates = empty_supported_rates;
	iodev->supported_channel_counts = empty_supported_channel_counts;
	iodev->buffer_size = EMPTY_BUFFER_SIZE;
	return iodev;
}

void empty_iodev_destroy(struct cras_iodev *iodev)
{
	struct cras_iodev_msg msg;

	msg.id = CRAS_IODEV_STOP;
	msg.length = sizeof(msg);
	cras_iodev_post_message_to_playback_thread(iodev, &msg);
	pthread_join(iodev->tid, NULL);
	cras_iodev_deinit(iodev);
	if (iodev->direction == CRAS_STREAM_INPUT)
		cras_iodev_list_rm_input(iodev);
	else {
		assert(iodev->direction == CRAS_STREAM_OUTPUT);
		cras_iodev_list_rm_output(iodev);
	}
	free(iodev);
}
