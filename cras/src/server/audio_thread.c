/* Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <pthread.h>
#include <sys/param.h>
#include <syslog.h>

#include "cras_config.h"
#include "cras_dsp.h"
#include "cras_dsp_pipeline.h"
#include "cras_fmt_conv.h"
#include "cras_iodev.h"
#include "cras_loopback_iodev.h"
#include "cras_metrics.h"
#include "cras_mix.h"
#include "cras_rstream.h"
#include "cras_server_metrics.h"
#include "cras_system_state.h"
#include "cras_types.h"
#include "cras_util.h"
#include "audio_thread.h"
#include "softvol_curve.h"
#include "utlist.h"

#define MIN_PROCESS_TIME_US 500 /* 0.5ms - min amount of time to mix/src. */
#define SLEEP_FUZZ_FRAMES 10 /* # to consider "close enough" to sleep frames. */
#define MIN_READ_WAIT_US 2000 /* 2ms */
#define DSP_PROCESS_BUF_BYTES 16384 /* 4k frames of stereo S16_LE */

static uint8_t dsp_process_buf[DSP_PROCESS_BUF_BYTES];

/* Messages that can be sent from the main context to the audio thread. */
enum AUDIO_THREAD_COMMAND {
	AUDIO_THREAD_ADD_ACTIVE_DEV,
	AUDIO_THREAD_ADD_STREAM,
	AUDIO_THREAD_DISCONNECT_STREAM,
	AUDIO_THREAD_RM_ACTIVE_DEV,
	AUDIO_THREAD_RM_STREAM,
	AUDIO_THREAD_RM_ALL_STREAMS,
	AUDIO_THREAD_SET_ACTIVE_DEV,
	AUDIO_THREAD_STOP,
	AUDIO_THREAD_DUMP_THREAD_INFO,
	AUDIO_THREAD_METRICS_LOG,
};

enum AUDIO_THREAD_METRICS_TYPE {
	LONGEST_TIMEOUT_MSECS,
};

struct audio_thread_msg {
	size_t length;
	enum AUDIO_THREAD_COMMAND id;
};

struct audio_thread_active_device_msg {
	struct audio_thread_msg header;
	struct cras_iodev *dev;
};

struct audio_thread_add_rm_stream_msg {
	struct audio_thread_msg header;
	struct cras_rstream *stream;
	enum CRAS_STREAM_DIRECTION dir;
};

struct audio_thread_dump_debug_info_msg {
	struct audio_thread_msg header;
	struct audio_debug_info *info;
};

struct audio_thread_metrics_log_msg {
	struct audio_thread_msg header;
	enum AUDIO_THREAD_METRICS_TYPE type;
	int stream_id;
	int arg;
};

/* For capture, the amount of frames that will be left after a read is
 * performed.  Sleep this many frames past the buffer size to be sure at least
 * the buffer size is captured when the audio thread wakes up.
 */
static const unsigned int CAP_REMAINING_FRAMES_TARGET = 16;

static struct iodev_callback_list *iodev_callbacks;

struct iodev_callback_list {
	int fd;
	thread_callback cb;
	void *cb_data;
	struct iodev_callback_list *prev, *next;
};

void audio_thread_add_callback(int fd, thread_callback cb,
                               void *data)
{
	struct iodev_callback_list *iodev_cb;

	/* Don't add iodev_cb twice */
	DL_FOREACH(iodev_callbacks, iodev_cb)
		if (iodev_cb->fd == fd && iodev_cb->cb_data == data)
			return;

	iodev_cb = (struct iodev_callback_list *)calloc(1, sizeof(*iodev_cb));
	iodev_cb->fd = fd;
	iodev_cb->cb = cb;
	iodev_cb->cb_data = data;

	DL_APPEND(iodev_callbacks, iodev_cb);
}

void audio_thread_rm_callback(int fd)
{
	struct iodev_callback_list *iodev_cb;

	DL_FOREACH(iodev_callbacks, iodev_cb) {
		if (iodev_cb->fd == fd) {
			DL_DELETE(iodev_callbacks, iodev_cb);
			free(iodev_cb);
			return;
		}
	}
}

/* Audio thread logging. */
static struct audio_thread_event_log *atlog;

static inline
struct audio_thread_event_log *audio_thread_event_log_init()
{
	return calloc(1, sizeof(struct audio_thread_event_log));
}

static inline
void audio_thread_event_log_deinit(struct audio_thread_event_log *log)
{
	free(log);
}

static inline void audio_thread_write_word(
		struct audio_thread_event_log *log,
		uint32_t word)
{
	log->log[log->write_pos] = word;
	log->write_pos++;
	log->write_pos %= AUDIO_THREAD_EVENT_LOG_SIZE;
}

/* Log a tag and the current time, Uses two words, the first is split
 * 8 bits for tag and 24 for seconds, second word is micro seconds.
 */
static inline void audio_thread_event_log_tag(
		struct audio_thread_event_log *log,
		enum AUDIO_THREAD_LOG_EVENTS event)
{
	struct timespec ts;

	clock_gettime(CLOCK_MONOTONIC, &ts);

	audio_thread_write_word(log, (event << 24) | (ts.tv_sec & 0x00ffffff));
	audio_thread_write_word(log, ts.tv_nsec);
}

static inline void audio_thread_event_log_data(
		struct audio_thread_event_log *log,
		enum AUDIO_THREAD_LOG_EVENTS event,
		uint32_t data)
{
	audio_thread_event_log_tag(log, event);
	audio_thread_write_word(log, data);
}

static inline void audio_thread_event_log_data2(
		struct audio_thread_event_log *log,
		enum AUDIO_THREAD_LOG_EVENTS event,
		uint32_t data,
		uint32_t data2)
{
	audio_thread_event_log_tag(log, event);
	audio_thread_write_word(log, data);
	audio_thread_write_word(log, data2);
}

static inline void audio_thread_event_log_data3(
		struct audio_thread_event_log *log,
		enum AUDIO_THREAD_LOG_EVENTS event,
		uint32_t data,
		uint32_t data2,
		uint32_t data3)
{
	audio_thread_event_log_tag(log, event);
	audio_thread_write_word(log, data);
	audio_thread_write_word(log, data2);
	audio_thread_write_word(log, data3);
}

/* Returns true if there are streams attached to the thread. */
static inline int streams_attached(const struct audio_thread *thread)
{
	return thread->streams != NULL;
}

static inline int output_streams_attached(const struct audio_thread *thread)
{
	if (thread->streams == NULL)
		return 0;

	struct cras_io_stream *curr;

	DL_FOREACH(thread->streams, curr)
		if (cras_stream_uses_output_hw(curr->stream->direction))
			return 1;

	return 0;
}

static inline int device_open(const struct cras_iodev *iodev)
{
	if (iodev && iodev->is_open(iodev))
		return 1;
	return 0;
}

static inline int stream_uses_direction(struct cras_rstream *stream,
					enum CRAS_STREAM_DIRECTION direction)
{
	if (direction == CRAS_STREAM_POST_MIX_PRE_DSP)
		return stream->direction == direction;
	return stream->direction == direction ||
	       stream->direction == CRAS_STREAM_UNIFIED;
}

/* Finds the lowest latency stream attached to the thread. */
static struct cras_io_stream *
get_min_latency_stream(const struct audio_thread *thread,
		       enum CRAS_STREAM_DIRECTION direction)
{
	struct cras_io_stream *lowest, *curr;

	lowest = NULL;
	DL_FOREACH(thread->streams, curr) {
		if (!stream_uses_direction(curr->stream, direction))
			continue;
		if (!lowest ||
		    (cras_rstream_get_buffer_size(curr->stream) <
		     cras_rstream_get_buffer_size(lowest->stream)))
			lowest = curr;
	}

	return lowest;
}

/* Sends a response (error code) from the audio thread to the main thread.
 * Indicates that the last message sent to the audio thread has been handled
 * with an error code of rc.
 * Args:
 *    thread - thread responding to command.
 *    rc - Result code to send back to the main thread.
 * Returns:
 *    The number of bytes written to the main thread.
 */
static int audio_thread_send_response(struct audio_thread *thread, int rc)
{
	return write(thread->to_main_fds[1], &rc, sizeof(rc));
}

/* Reads a command from the main thread.  Called from the playback/capture
 * thread.  This will read the next available command from the main thread and
 * put it in buf.
 * Args:
 *    thread - thread reading the command.
 *    buf - Message is stored here on return.
 *    max_len - maximum length of message to put into buf.
 * Returns:
 *    0 on success, negative error code on failure.
 */
static int audio_thread_read_command(struct audio_thread *thread,
				     uint8_t *buf,
				     size_t max_len)
{
	int to_read, nread, rc;
	struct audio_thread_msg *msg = (struct audio_thread_msg *)buf;

	/* Get the length of the message first */
	nread = read(thread->to_thread_fds[0], buf, sizeof(msg->length));
	if (nread < 0)
		return nread;
	if (msg->length > max_len)
		return -ENOMEM;

	to_read = msg->length - nread;
	rc = read(thread->to_thread_fds[0], &buf[0] + nread, to_read);
	if (rc < 0)
		return rc;
	return 0;
}

/* Posts metrics log message for the longest timeout of a stream.
 */
static int audio_thread_log_longest_timeout(struct audio_thread *thread,
					    int timeout_msec)
{
	struct audio_thread_metrics_log_msg msg;
	int rc;

	msg.header.id = AUDIO_THREAD_METRICS_LOG;
	msg.header.length = sizeof(msg);
	msg.type = LONGEST_TIMEOUT_MSECS;
	msg.arg = timeout_msec;

	rc = write(thread->main_msg_fds[1], &msg, sizeof(msg));
	return rc;
}

/* Checks if there are any active streams.
 * Args:
 *    thread - The thread to check.
 *    in_active - Set to the number of active input streams.
 *    out_active - Set to the number of active output streams.
 * Returns:
 *    0 if no input or output streams are active, 1 if there are active streams.
 */
static int active_streams(const struct audio_thread *thread,
			  int *in_active,
			  int *out_active,
			  int *loop_active)
{
	struct cras_io_stream *curr;

	*in_active = 0;
	*out_active = 0;
	*loop_active = 0;

	DL_FOREACH(thread->streams, curr) {
		if (stream_uses_input(curr->stream))
			*in_active = *in_active + 1;
		if (stream_uses_output(curr->stream))
			*out_active = *out_active + 1;
		if (stream_uses_loopback(curr->stream))
			*loop_active = *loop_active + 1;
	}

	return *in_active || *out_active || *loop_active;
}

static int append_stream(struct audio_thread *thread,
			 struct cras_rstream *stream)
{
	struct cras_io_stream *out;

	/* Check that we don't already have this stream */
	DL_SEARCH_SCALAR(thread->streams, out, stream, stream);
	if (out != NULL)
		return -EEXIST;

	/* New stream, allocate a container and add it to the list. */
	out = calloc(1, sizeof(*out));
	if (out == NULL)
		return -ENOMEM;
	out->stream = stream;
	out->fd = cras_rstream_get_audio_fd(stream);
	DL_APPEND(thread->streams, out);

	return 0;
}

static int delete_stream(struct audio_thread *thread,
			 struct cras_rstream *stream)
{
	struct cras_io_stream *out;
	int longest_timeout_msec;
	struct cras_audio_shm *shm;

	/* Find stream, and if found, delete it. */
	DL_SEARCH_SCALAR(thread->streams, out, stream, stream);
	if (out == NULL)
		return -EINVAL;

	/* Log the longest timeout of the stream about to be removed. */
	if (stream_uses_output(stream)) {
		shm = cras_rstream_output_shm(stream);
		longest_timeout_msec = cras_shm_get_longest_timeout(shm);
		if (longest_timeout_msec)
			audio_thread_log_longest_timeout(thread, longest_timeout_msec);
	}

	DL_DELETE(thread->streams, out);
	if (stream->client == NULL)
		cras_rstream_destroy(stream);
	free(out);

	return 0;
}

/* Gets the first active device for given direction. */
static inline
struct cras_iodev *first_active_device(const struct audio_thread *thread,
				       enum CRAS_STREAM_DIRECTION dir)
{
	return thread->active_devs[dir]
			? thread->active_devs[dir]->dev
			: NULL;
}

static inline
struct cras_iodev *first_output_dev(const struct audio_thread *thread)
{
	return first_active_device(thread, CRAS_STREAM_OUTPUT);
}

static inline
struct cras_iodev *first_input_dev(const struct audio_thread *thread)
{
	return first_active_device(thread, CRAS_STREAM_INPUT);
}

static inline
struct cras_iodev *first_loop_dev(const struct audio_thread *thread)
{
	return first_active_device(thread, CRAS_STREAM_POST_MIX_PRE_DSP);
}

/* Close a device if it's been opened. */
static inline int close_device(struct cras_iodev *dev)
{
	if (!dev->is_open(dev))
		return 0;
	return dev->close_dev(dev);
}

/* Close active devices. */
static int close_devices(struct audio_thread *thread,
			 enum CRAS_STREAM_DIRECTION dir)
{
	struct active_dev *adev;
	int err = 0;
	int rc;

	DL_FOREACH(thread->active_devs[dir], adev) {
		rc = close_device(adev->dev);
		if (rc)
			err = rc;
	}
	thread->devs_open[dir] = 0;
	thread->buffer_frames[dir] = 0;
	thread->cb_threshold[dir] = 0;
	return err;
}

/* Open the device configured to play the format of the given stream. */
static int init_device(struct cras_iodev *dev, struct cras_rstream *stream)
{
	struct cras_audio_format fmt;
	cras_rstream_get_format(stream, &fmt);
	cras_iodev_set_format(dev, &fmt);
	return dev->open_dev(dev);
}

void thread_rm_active_dev(struct audio_thread *thread,
			  struct cras_iodev *iodev);

/* Initialize active devices of given direction. */
static int init_devices(struct audio_thread *thread,
			enum CRAS_STREAM_DIRECTION dir,
			struct cras_rstream *stream)
{
	struct active_dev *adev;
	struct active_dev *adevs;
	struct cras_iodev *first_dev;
	int rc;

	adevs = thread->active_devs[dir];
	if (!adevs)
		return -EINVAL;

	/* Initialize the first active device. If it returns error,
	 * don't bother initialize other active devices.
	 */
	first_dev = adevs->dev;
	rc = init_device(first_dev, stream);
	if (rc)
		return rc;

	/* Initialize all other active devices of the same direction.
	 * If the supported format does not match what was used for
	 * selected device, close and remove it from the active device
	 * list.
	 */
	adevs = adevs->next;
	DL_FOREACH(adevs, adev) {
		rc = init_device(adev->dev, stream);
		if (rc) {
			syslog(LOG_ERR, "Failed to open %s",
					adev->dev->info.name);
			thread_rm_active_dev(thread, adev->dev);
			continue;
		}
		if (cras_fmt_conversion_needed(first_dev->format,
					       adev->dev->format)) {
			close_device(adev->dev);
			thread_rm_active_dev(thread, adev->dev);
		}
	}
	thread->devs_open[dir] = 1;
	return 0;
}

/* Checks all active devices are playing or recording. Return 0
 * if there's no active device present or any active device is not
 * running, return 1 otherwise.
 */
static int devices_running(struct audio_thread *thread,
			   enum CRAS_STREAM_DIRECTION dir)
{
	struct active_dev *adev;
	struct active_dev *adevs;

	adevs = thread->active_devs[dir];
	if (adevs == NULL)
		return 0;
	DL_FOREACH(adevs, adev)
		if (!adev->dev->dev_running(adev->dev))
			return 0;
	return 1;
}

void config_devices_min_latency(struct audio_thread *thread,
				enum CRAS_STREAM_DIRECTION dir)
{
	struct cras_io_stream *min_latency;

	min_latency = get_min_latency_stream(thread, dir);
	thread->cb_threshold[dir] =
			cras_rstream_get_cb_threshold(min_latency->stream);
	thread->buffer_frames[dir] =
			cras_rstream_get_buffer_size(min_latency->stream);
	if ((dir == CRAS_STREAM_OUTPUT) &&
	    thread->cb_threshold[dir] > thread->buffer_frames[dir] / 2)
		thread->cb_threshold[dir] = thread->buffer_frames[dir] / 2;
}

/*
 * Non-static functions of prefix 'thread_' runs in audio thread
 * to manipulate iodevs and streams, visible for unittest.
 */

/* Clears all active devices for given direction, used to handle
 * message from main thread to set a new active device.
 */
void thread_clear_active_devs(struct audio_thread *thread,
			      enum CRAS_STREAM_DIRECTION dir)
{
	struct active_dev *adev;
	DL_FOREACH(thread->active_devs[dir], adev) {
		DL_DELETE(thread->active_devs[dir], adev);
		adev->dev->is_active = 0;
		free(adev);
	}
}

/* Handles message from main thread to set or add a new active device. */
void thread_add_active_dev(struct audio_thread *thread,
			   struct cras_iodev *iodev)
{
	struct active_dev *adev;
	DL_SEARCH_SCALAR(thread->active_devs[iodev->direction],
			 adev, dev, iodev);
	if (adev) {
		syslog(LOG_ERR, "Device %s already active",
		       adev->dev->info.name);
		return;
	}
	adev = (struct active_dev *)calloc(1, sizeof(*adev));
	adev->dev = iodev;
	iodev->is_active = 1;
	DL_APPEND(thread->active_devs[iodev->direction], adev);
}

/* Handles message from main thread to remove an active device. */
void thread_rm_active_dev(struct audio_thread *thread,
			  struct cras_iodev *iodev)
{
	struct active_dev *adev;
	DL_FOREACH(thread->active_devs[iodev->direction], adev) {
		if (adev->dev == iodev) {
			DL_DELETE(thread->active_devs[iodev->direction], adev);
			iodev->is_active = 0;
			free(adev);
		}
	}
}

/* Remove stream from the audio thread. If this is the last stream to be
 * removed close the device. Returns the number of streams still attached
 * to the thread.
 */
int thread_remove_stream(struct audio_thread *thread,
			 struct cras_rstream *stream)
{
	int in_active, out_active, loop_active;

	if (delete_stream(thread, stream))
		syslog(LOG_ERR, "Stream to remove not found.");

	active_streams(thread, &in_active, &out_active, &loop_active);

	/* When removing the last stream of a certain direction, close
	 * all active devices of that direction. Otherwise reconfigure
	 * active devices for the min latency stream.
	 */
	if (!in_active)
		close_devices(thread, CRAS_STREAM_INPUT);
	else
		config_devices_min_latency(thread, CRAS_STREAM_INPUT);

	if (!out_active)
		close_devices(thread, CRAS_STREAM_OUTPUT);
	else
		config_devices_min_latency(thread, CRAS_STREAM_OUTPUT);

	if (!loop_active)
		close_devices(thread, CRAS_STREAM_POST_MIX_PRE_DSP);
	else
		config_devices_min_latency(thread,
					   CRAS_STREAM_POST_MIX_PRE_DSP);

	return streams_attached(thread);
}


/* Handles the disconnect_stream message from the main thread. */
int thread_disconnect_stream(struct audio_thread* thread,
			     struct cras_rstream* stream) {
	struct cras_io_stream *out;

	stream->client = NULL;
	stream->fd = -1;

	/* If the stream has been removed from active streams, destroy it. */
	DL_SEARCH_SCALAR(thread->streams, out, stream, stream);
	if (out == NULL) {
		cras_rstream_destroy(stream);
		return 0;
	}

	/* Keep output stream alive to drain the audio in the buffer properly.
	 * We will remove it when the audio has been consumed. */
	if (!stream_uses_output(stream))
		return thread_remove_stream(thread, stream);
	return streams_attached(thread);
}

/* Put 'frames' worth of zero samples into odev. */
void fill_odev_zeros(struct cras_iodev *odev, unsigned int frames)
{
	uint8_t *dst;
	unsigned int frame_bytes;
	int rc;

	frame_bytes = cras_get_format_bytes(odev->format);

	rc = odev->get_buffer(odev, &dst, &frames);
	if (rc < 0)
		return;

	memset(dst, 0, frames * frame_bytes);
	odev->put_buffer(odev, frames);
}

/* Builds an initial buffer to avoid an underrun. Adds cb_threshold latency. */
void fill_odevs_zeros_cb_threshold(struct audio_thread *thread)
{
	struct active_dev *adev;
	DL_FOREACH(thread->active_devs[CRAS_STREAM_OUTPUT], adev)
		fill_odev_zeros(adev->dev,
				thread->cb_threshold[CRAS_STREAM_OUTPUT]);
}

/* Handles the add_stream message from the main thread. */
int thread_add_stream(struct audio_thread *thread,
		      struct cras_rstream *stream)
{
	struct cras_iodev *loop_dev = first_loop_dev(thread);
	int rc;

	rc = append_stream(thread, stream);
	if (rc < 0)
		return AUDIO_THREAD_ERROR_OTHER;

	/* If not already, open the device(s). */
	if (stream_uses_output(stream) &&
	    !thread->devs_open[CRAS_STREAM_OUTPUT]) {
		rc = init_devices(thread, CRAS_STREAM_OUTPUT, stream);
		if (rc < 0) {
			delete_stream(thread, stream);
			return AUDIO_THREAD_OUTPUT_DEV_ERROR;
		}

		if (cras_stream_is_unified(stream->direction))
			/* Start unified streams by padding the output.
			 * This avoid underruns while processing the input data.
			 */
			fill_odevs_zeros_cb_threshold(thread);

		if (loop_dev) {
			struct cras_io_stream *iostream;
			struct cras_audio_format fmt;

			cras_rstream_get_format(stream, &fmt);
			loopback_iodev_set_format(loop_dev, &fmt);
			/* For each loopback stream; detach and tell client to reconfig. */
			DL_FOREACH(thread->streams, iostream) {
				if (!stream_uses_loopback(iostream->stream))
					continue;
				cras_rstream_send_client_reattach(iostream->stream);
				thread_remove_stream(thread, iostream->stream);
			}
		}
	}
	if (stream_uses_input(stream) &&
	    !thread->devs_open[CRAS_STREAM_INPUT]) {
		rc = init_devices(thread, CRAS_STREAM_INPUT, stream);
		if (rc < 0) {
			delete_stream(thread, stream);
			return AUDIO_THREAD_INPUT_DEV_ERROR;
		}
	}
	if (stream_uses_loopback(stream) &&
	    !thread->devs_open[CRAS_STREAM_POST_MIX_PRE_DSP]) {
		rc = init_devices(thread, CRAS_STREAM_POST_MIX_PRE_DSP, stream);
		if (rc < 0) {
			delete_stream(thread, stream);
			return AUDIO_THREAD_LOOPBACK_DEV_ERROR;
		}
	}

	if (thread->devs_open[CRAS_STREAM_OUTPUT])
		config_devices_min_latency(thread, CRAS_STREAM_OUTPUT);

	if (thread->devs_open[CRAS_STREAM_INPUT])
		config_devices_min_latency(thread, CRAS_STREAM_INPUT);

	if (thread->devs_open[CRAS_STREAM_POST_MIX_PRE_DSP])
		config_devices_min_latency(thread,
					   CRAS_STREAM_POST_MIX_PRE_DSP);

	return 0;
}

static int has_dsp(struct cras_iodev *iodev) {
	struct cras_dsp_context *ctx;
	struct pipeline *pipeline;

	ctx = iodev->dsp_context;
	if (!ctx)
		return 0;
	pipeline = cras_dsp_get_pipeline(ctx);
	if (!pipeline)
		return 0;
	cras_dsp_put_pipeline(ctx);
	return 1;
}

static void apply_dsp(struct cras_iodev *iodev, uint8_t *buf, size_t frames)
{
	struct cras_dsp_context *ctx;
	struct pipeline *pipeline;

	ctx = iodev->dsp_context;
	if (!ctx)
		return;

	pipeline = cras_dsp_get_pipeline(ctx);
	if (!pipeline)
		return;

	cras_dsp_pipeline_apply(pipeline,
				iodev->format->num_channels,
				buf,
				frames);

	cras_dsp_put_pipeline(ctx);
}

static int get_dsp_delay(struct cras_iodev *iodev)
{
	struct cras_dsp_context *ctx;
	struct pipeline *pipeline;
	int delay;

	ctx = iodev->dsp_context;
	if (!ctx)
		return 0;

	pipeline = cras_dsp_get_pipeline(ctx);
	if (!pipeline)
		return 0;

	delay = cras_dsp_pipeline_get_delay(pipeline);

	cras_dsp_put_pipeline(ctx);
	return delay;
}

/* Reads any pending audio message from the socket. */
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

/* Calculates the length of timeout period and updates the longest
 * timeout value.
 */
static void update_stream_timeout(struct cras_audio_shm *shm)
{
	struct timespec diff;
	int timeout_msec = 0;
	int longest_timeout_msec;

	cras_shm_since_first_timeout(shm, &diff);
	if (!diff.tv_sec && !diff.tv_nsec)
		return;

	timeout_msec = diff.tv_sec * 1000 + diff.tv_nsec / 1000000;
	longest_timeout_msec = cras_shm_get_longest_timeout(shm);
	if (timeout_msec > longest_timeout_msec)
		cras_shm_set_longest_timeout(shm, timeout_msec);
}

/* Removes those streams which are both disconnected and empty. */
static int remove_empty_streams(struct audio_thread *thread)
{
	struct cras_io_stream *curr;
	struct cras_audio_shm *shm;
	int frames_in_buff;
	DL_FOREACH(thread->streams, curr) {
		if (curr->stream->client != NULL ||
		    !stream_uses_output(curr->stream))
			continue;
		shm = cras_rstream_output_shm(curr->stream);
		frames_in_buff = cras_shm_get_frames(shm);
		if (frames_in_buff < 0)
			return frames_in_buff;
		if (frames_in_buff == 0)
			thread_remove_stream(thread, curr->stream);
	}
	return 0;
}

/* Asks any stream with room for more data. Sets the time stamp for all streams.
 * Args:
 *    thread - The thread to fetch samples for.
 *    hw_level - Amount of data in the hw buffer.
 *    delay - How much latency is queued to the device (frames).
 * Returns:
 *    0 on success, negative error on failure. If failed, can assume that all
 *    streams have been removed from the device.
 */
static int fetch_and_set_timestamp(struct audio_thread *thread,
				   unsigned int hw_level,
				   size_t delay)
{
	struct cras_iodev *odev = first_output_dev(thread);
	size_t fr_rate;
	int frames_in_buff;
	struct cras_io_stream *curr;
	int rc;

	fr_rate = odev->format->frame_rate;

	DL_FOREACH(thread->streams, curr) {
		struct cras_audio_shm *shm =
			cras_rstream_output_shm(curr->stream);

		if (!stream_uses_output(curr->stream))
			continue;

		if (cras_shm_callback_pending(shm))
			flush_old_aud_messages(shm, curr->fd);

		if (curr->stream->direction != CRAS_STREAM_OUTPUT)
			continue;

		frames_in_buff = cras_shm_get_frames(shm);
		if (frames_in_buff < 0)
			return frames_in_buff;

		cras_iodev_set_playback_timestamp(fr_rate,
						  frames_in_buff + delay,
						  &shm->area->ts);

		/* If we already have enough data, don't poll this stream. */
		if (frames_in_buff + hw_level >
		    cras_rstream_get_cb_threshold(curr->stream) +
				SLEEP_FUZZ_FRAMES)
			continue;

		if (!cras_shm_callback_pending(shm) &&
		    cras_shm_is_buffer_available(shm)) {
			audio_thread_event_log_data2(
				atlog,
				AUDIO_THREAD_FETCH_STREAM,
				curr->stream->stream_id,
				cras_rstream_get_cb_threshold(curr->stream));
			rc = cras_rstream_request_audio(curr->stream);
			if (rc < 0) {
				thread_remove_stream(thread, curr->stream);
				/* If this failed and was the last stream,
				 * return, otherwise, on to the next one */
				if (!output_streams_attached(thread))
					return -EIO;
				continue;
			}
			update_stream_timeout(shm);
			cras_shm_clear_first_timeout(shm);
			cras_shm_set_callback_pending(shm, 1);
		}
	}

	return 0;
}

/* Check if the stream kept timeout for a long period.
 * Args:
 *    stream - The stream to check
 *    rate - the frame rate of iodev
 * Returns:
 *    0 if this is a first timeout or the accumulated timeout
 *    period is not too long, -1 otherwise.
 */
static int check_stream_timeout(struct cras_rstream *stream, unsigned int rate)
{
	static int longest_cb_timeout_sec = 10;

	struct cras_audio_shm *shm;
	struct timespec diff;
	shm = cras_rstream_output_shm(stream);

	cras_shm_since_first_timeout(shm, &diff);

	if (!diff.tv_sec && !diff.tv_nsec) {
		cras_shm_set_first_timeout(shm);
		return 0;
	}

	return (diff.tv_sec > longest_cb_timeout_sec) ? -1 : 0;
}

/* Fill the buffer with samples from the attached streams.
 * Args:
 *    thread - The thread to write streams from.
 *    dst - The buffer to put the samples in (returned from snd_pcm_mmap_begin)
 *    level - The number of frames still in device buffer.
 *    write_limit - The maximum number of frames to write to dst.
 *
 * Returns:
 *    The number of frames rendered on success, a negative error code otherwise.
 *    This number of frames is the minimum of the amount of frames each stream
 *    could provide which is the maximum that can currently be rendered.
 */
static int write_streams(struct audio_thread *thread,
			 uint8_t *dst,
			 size_t level,
			 size_t write_limit)
{
	struct cras_iodev *odev = first_output_dev(thread);
	struct cras_io_stream *curr;
	struct timeval to;
	fd_set poll_set, this_set;
	size_t streams_wait, num_mixed;
	size_t input_write_limit = write_limit;
	int max_fd;
	int rc;
	int max_frames = 0;

	/* Timeout on reading before we under-run. Leaving time to mix. */
	to.tv_sec = 0;
	to.tv_usec = (level * 1000000 / odev->format->frame_rate);
	if (to.tv_usec > MIN_PROCESS_TIME_US)
		to.tv_usec -= MIN_PROCESS_TIME_US;
	if (to.tv_usec < MIN_READ_WAIT_US)
		to.tv_usec = MIN_READ_WAIT_US;

	FD_ZERO(&poll_set);
	max_fd = -1;
	streams_wait = 0;
	num_mixed = 0;

	/* Check if streams have enough data to fill this request,
	 * if not, wait for them. Mix all streams we have enough data for. */
	DL_FOREACH(thread->streams, curr) {
		struct cras_audio_shm *shm;
		int shm_frames;

		if (!stream_uses_output(curr->stream) ||
		    curr->stream->client == NULL)
			continue;

		curr->skip_mix = 0;

		shm = cras_rstream_output_shm(curr->stream);

		shm_frames = cras_shm_get_frames(shm);
		audio_thread_event_log_data3(atlog,
					     AUDIO_THREAD_WRITE_STREAMS_STREAM,
					     curr->stream->stream_id,
					     shm_frames,
					     cras_shm_callback_pending(shm));
		if (shm_frames < 0) {
			thread_remove_stream(thread, curr->stream);
			if (!output_streams_attached(thread))
				return -EIO;
		} else if (cras_shm_callback_pending(shm)) {
			/* Callback pending, wait for a response. */
			streams_wait++;
			FD_SET(curr->fd, &poll_set);
			if (curr->fd > max_fd)
				max_fd = curr->fd;
		}
	}

	/* Wait until all polled clients reply, or a timeout. */
	while (streams_wait > 0) {
		this_set = poll_set;
		audio_thread_event_log_data2(atlog,
					     AUDIO_THREAD_WRITE_STREAMS_WAIT,
					     to.tv_sec,
					     to.tv_usec);
		rc = select(max_fd + 1, &this_set, NULL, NULL, &to);
		if (rc < 0) {
			if (rc == -EINTR)
				continue;
			syslog(LOG_ERR, "select error %d", rc);
			break;
		} else if (rc == 0) {
			audio_thread_event_log_tag(
				atlog,
				AUDIO_THREAD_WRITE_STREAMS_WAIT_TO);
			/* Timeout */
			DL_FOREACH(thread->streams, curr) {
				struct cras_audio_shm *shm;

				if (!stream_uses_output(curr->stream))
					continue;

				shm = cras_rstream_output_shm(curr->stream);

				if (!cras_shm_callback_pending(shm) ||
				    !FD_ISSET(curr->fd, &poll_set))
					continue;

				cras_shm_inc_cb_timeouts(shm);
				if (check_stream_timeout(
						curr->stream,
						odev->format->frame_rate)) {
					update_stream_timeout(shm);
					thread_remove_stream(thread,
							     curr->stream);
					if (!output_streams_attached(thread))
						return -EIO;
				}
				if (cras_shm_get_frames(shm) == 0)
					curr->skip_mix = 1;
			}
			break;
		}
		DL_FOREACH(thread->streams, curr) {
			struct cras_audio_shm *shm;

			if (!stream_uses_output(curr->stream))
				continue;

			if (!FD_ISSET(curr->fd, &this_set))
				continue;

			shm = cras_rstream_output_shm(curr->stream);

			FD_CLR(curr->fd, &poll_set);
			streams_wait--;
			cras_shm_set_callback_pending(shm, 0);
			rc = cras_rstream_get_audio_request_reply(curr->stream);
			if (rc < 0) {
				thread_remove_stream(thread, curr->stream);
				if (!output_streams_attached(thread))
					return -EIO;
				continue;
			}
			/* Skip mixing if it returned zero frames. */
			if (cras_shm_get_frames(shm) == 0)
				curr->skip_mix = 1;
		}
	}

	/* Mix as much as we can, the minimum fill level of any stream. */
	DL_FOREACH(thread->streams, curr) {
		struct cras_audio_shm *shm;
		int shm_frames;

		if (!cras_stream_uses_output_hw(curr->stream->direction))
			continue;
		shm = cras_rstream_output_shm(curr->stream);

		shm_frames = cras_shm_get_frames(shm);
		if (shm_frames < 0) {
			thread_remove_stream(thread, curr->stream);
			if (!output_streams_attached(thread))
				return -EIO;
		} else if (!cras_shm_callback_pending(shm) && !curr->skip_mix) {
			/* If not in underrun, use this stream. */
			write_limit = MIN(shm_frames, write_limit);
			max_frames = MAX(max_frames, shm_frames);
		}
	}

	audio_thread_event_log_data(atlog, AUDIO_THREAD_WRITE_STREAMS_MIX,
				    write_limit);

	if (max_frames == 0 &&
	    (odev->frames_queued(odev) <=
		    thread->cb_threshold[CRAS_STREAM_OUTPUT] / 4)) {
		/* Nothing to mix from any streams. Under run. */
		unsigned int frame_bytes = cras_get_format_bytes(odev->format);
		size_t frames = MIN(thread->cb_threshold[CRAS_STREAM_OUTPUT],
				    input_write_limit);
		return cras_mix_mute_buffer(dst, frame_bytes, frames);
	}

	DL_FOREACH(thread->streams, curr) {
		struct cras_audio_shm *shm;
		if (!cras_stream_uses_output_hw(curr->stream->direction))
			continue;
		shm = cras_rstream_output_shm(curr->stream);
		if (cras_mix_add_stream(shm,
					odev->format->num_channels,
					dst, &write_limit, &num_mixed))
			cras_shm_buffer_read(shm, write_limit);
	}

	audio_thread_event_log_data2(atlog, AUDIO_THREAD_WRITE_STREAMS_MIXED,
				     write_limit, num_mixed);
	if (num_mixed == 0)
		return num_mixed;

	return write_limit;
}

/* Checks if the stream type matches the device.  For input devices, both input
 * and unified streams match, for loopback, each loopback type matches.
 */
static int input_stream_matches_dev(enum CRAS_STREAM_DIRECTION dir,
				    struct cras_rstream *rstream)
{
	if (dir == CRAS_STREAM_INPUT)
		return stream_uses_input(rstream);

	if (dir == CRAS_STREAM_POST_MIX_PRE_DSP)
		return rstream->direction == CRAS_STREAM_POST_MIX_PRE_DSP;

	return 0;
}

/* Gets the max delay frames of active input devices. */
int input_delay_frames(struct active_dev *adevs)
{
	struct active_dev *adev;
	int delay;
	int max_delay = 0;

	DL_FOREACH(adevs, adev) {
		delay = adev->dev->delay_frames(adev->dev) +
				get_dsp_delay(adev->dev);
		if (delay < 0)
			return delay;
		if (delay > max_delay)
			max_delay = delay;
	}
	return max_delay;
}

/* Gets the min queued frames of active input devices. */
int input_min_frames_queued(struct active_dev *adevs)
{
	struct active_dev *adev;
	int min_frames_queued;
	int fr;

	min_frames_queued = adevs->dev->frames_queued(adevs->dev);
	if (min_frames_queued < 0)
		return min_frames_queued;
	DL_FOREACH(adevs, adev) {
		fr = adev->dev->frames_queued(adev->dev);
		if (fr < 0)
			return fr;
		if (min_frames_queued > fr)
			min_frames_queued = fr;
	}

	return min_frames_queued;
}

/* Stop the playback thread */
static void terminate_pb_thread()
{
	pthread_exit(0);
}

/* Handle a message sent to the playback thread */
static int handle_playback_thread_message(struct audio_thread *thread)
{
	uint8_t buf[256];
	struct audio_thread_msg *msg = (struct audio_thread_msg *)buf;
	int ret = 0;
	int err;

	err = audio_thread_read_command(thread, buf, 256);
	if (err < 0)
		return err;

	switch (msg->id) {
	case AUDIO_THREAD_ADD_STREAM: {
		struct audio_thread_add_rm_stream_msg *amsg;
		amsg = (struct audio_thread_add_rm_stream_msg *)msg;
		audio_thread_event_log_data(
			atlog,
			AUDIO_THREAD_WRITE_STREAMS_WAIT,
			amsg->stream->stream_id);
		ret = thread_add_stream(thread, amsg->stream);
		break;
	}
	case AUDIO_THREAD_DISCONNECT_STREAM: {
		struct audio_thread_add_rm_stream_msg *rmsg;

		rmsg = (struct audio_thread_add_rm_stream_msg *)msg;

		ret = thread_disconnect_stream(thread, rmsg->stream);
		break;
	}
	case AUDIO_THREAD_RM_ALL_STREAMS: {
		struct cras_io_stream *iostream;
		struct audio_thread_add_rm_stream_msg *rmsg;
		enum CRAS_STREAM_DIRECTION dir;

		rmsg = (struct audio_thread_add_rm_stream_msg *)msg;
		dir = rmsg->dir;

		/* For each stream; detach and tell client to reconfig. */
		DL_FOREACH(thread->streams, iostream) {
			if (iostream->stream->direction != dir &&
				iostream->stream->direction
					!= CRAS_STREAM_UNIFIED)
				continue;
			cras_rstream_send_client_reattach(iostream->stream);
			thread_remove_stream(thread, iostream->stream);
		}
		break;
	}
	case AUDIO_THREAD_SET_ACTIVE_DEV: {
		struct audio_thread_active_device_msg *rmsg;
		struct cras_iodev *dev;

		rmsg = (struct audio_thread_active_device_msg *)msg;
		dev = rmsg->dev;
		thread_clear_active_devs(thread, dev->direction);
		thread_add_active_dev(thread, dev);
		break;
	}
	case AUDIO_THREAD_ADD_ACTIVE_DEV: {
		struct audio_thread_active_device_msg *rmsg;

		rmsg = (struct audio_thread_active_device_msg *)msg;
		thread_add_active_dev(thread, rmsg->dev);
		break;
	}
	case AUDIO_THREAD_RM_ACTIVE_DEV: {
		struct audio_thread_active_device_msg *rmsg;

		rmsg = (struct audio_thread_active_device_msg *)msg;
		thread_rm_active_dev(thread, rmsg->dev);
		break;
	}
	case AUDIO_THREAD_STOP:
		ret = 0;
		err = audio_thread_send_response(thread, ret);
		if (err < 0)
			return err;
		terminate_pb_thread();
		break;
	case AUDIO_THREAD_DUMP_THREAD_INFO: {
		struct cras_io_stream *curr;
		struct cras_iodev *idev = first_input_dev(thread);
		struct cras_iodev *odev = first_output_dev(thread);
		struct audio_thread_dump_debug_info_msg *dmsg;
		struct audio_debug_info *info;
		unsigned int i = 0;

		ret = 0;
		dmsg = (struct audio_thread_dump_debug_info_msg *)msg;
		info = dmsg->info;

		if (odev) {
			strncpy(info->output_dev_name, odev->info.name,
				sizeof(info->output_dev_name));
			info->output_buffer_size = odev->buffer_size;
			info->output_used_size =
					thread->buffer_frames[CRAS_STREAM_OUTPUT];
			info->output_cb_threshold =
					thread->cb_threshold[CRAS_STREAM_OUTPUT];
		} else {
			info->output_dev_name[0] = '\0';
			info->output_buffer_size = 0;
			info->output_used_size = 0;
			info->output_cb_threshold = 0;
		}
		if (idev) {
			strncpy(info->input_dev_name, idev->info.name,
				sizeof(info->input_dev_name));
			info->input_buffer_size = idev->buffer_size;
			info->input_used_size =
					thread->buffer_frames[CRAS_STREAM_INPUT];
			info->input_cb_threshold =
					thread->cb_threshold[CRAS_STREAM_INPUT];
		} else {
			info->output_dev_name[0] = '\0';
			info->output_buffer_size = 0;
			info->output_used_size = 0;
			info->output_cb_threshold = 0;
		}

		DL_FOREACH(thread->streams, curr) {
			struct cras_audio_shm *shm;
			struct audio_stream_debug_info *si;

			shm = stream_uses_output(curr->stream) ?
				cras_rstream_output_shm(curr->stream) :
				cras_rstream_input_shm(curr->stream);

			si = &info->streams[i];

			si->stream_id = curr->stream->stream_id;
			si->direction = curr->stream->direction;
			si->buffer_frames = curr->stream->buffer_frames;
			si->cb_threshold = curr->stream->cb_threshold;
			si->min_cb_level = curr->stream->min_cb_level;
			si->frame_rate = curr->stream->format.frame_rate;
			si->num_channels = curr->stream->format.num_channels;
			si->num_cb_timeouts = cras_shm_num_cb_timeouts(shm);
			memcpy(si->channel_layout,
			       curr->stream->format.channel_layout,
			       sizeof(si->channel_layout));

			if (++i == MAX_DEBUG_STREAMS)
				break;
		}
		info->num_streams = i;

		memcpy(&info->log, atlog, sizeof(info->log));
		break;
	}
	default:
		ret = -EINVAL;
		break;
	}

	err = audio_thread_send_response(thread, ret);
	if (err < 0)
		return err;
	return ret;
}

/* Adjusts the hw_level for output only streams.  Account for any extra
 * buffering that is needed and indicated by the min_buffer_level member.
 */
static unsigned int adjust_level(const struct audio_thread *thread, int level)
{
	struct cras_iodev *idev = first_input_dev(thread);
	struct cras_iodev *odev = first_output_dev(thread);

	if (device_open(idev) || !odev || odev->min_buffer_level == 0)
		return level;

	if (level > odev->min_buffer_level) {
		return level - odev->min_buffer_level;
	} else {
		/* If there has been an underrun, take the opportunity to re-pad
		 * the buffer by filling it with zeros. */
		if (level == 0)
			fill_odev_zeros(odev, odev->min_buffer_level);
		return 0;
	}
}

/* Returns the number of frames that can be slept for before an output stream
 * needs to be serviced.
 */
static int get_output_sleep_frames(struct audio_thread *thread)
{
	struct cras_iodev *odev = first_output_dev(thread);
	struct cras_io_stream *curr;
	unsigned int adjusted_level;
	unsigned int sleep_frames;
	int rc;

	rc = odev->frames_queued(odev);
	if (rc < 0)
		return rc;
	adjusted_level = adjust_level(thread, rc);

	if (adjusted_level < thread->cb_threshold[CRAS_STREAM_OUTPUT])
		return 0;
	sleep_frames = adjusted_level - thread->cb_threshold[CRAS_STREAM_OUTPUT];
	DL_FOREACH(thread->streams, curr) {
		struct cras_audio_shm *shm =
			cras_rstream_output_shm(curr->stream);
		unsigned int cb_thresh;
		int frames_in_buff;

		if (curr->stream->client == NULL ||
		    curr->stream->direction != CRAS_STREAM_OUTPUT)
			continue;

		cb_thresh = cras_rstream_get_cb_threshold(curr->stream);
		frames_in_buff = cras_shm_get_frames(shm);
		if (frames_in_buff < 0)
			return frames_in_buff;
		frames_in_buff += adjusted_level;
		if (frames_in_buff < cb_thresh)
			sleep_frames = 0;
		else
			sleep_frames = MIN(sleep_frames,
						 frames_in_buff - cb_thresh);
	}

	return sleep_frames;
}

/* Transfer samples from clients to the audio device.
 * Return the number of samples written to the device.
 * Args:
 *    thread - the audio thread this is running for.
 *    next_sleep_frames - filled with the minimum number of frames needed before
 *        the next wake up.
 */
int possibly_fill_audio(struct audio_thread *thread,
			unsigned int *next_sleep_frames)
{
	unsigned int frames, fr_to_req;
	snd_pcm_sframes_t written;
	snd_pcm_uframes_t total_written = 0;
	int rc;
	int delay;
	uint8_t *dst = NULL;
	struct cras_iodev *odev = first_output_dev(thread);
	struct cras_iodev *loop_dev = first_loop_dev(thread);
	unsigned int frame_bytes;
	unsigned int hw_level, adjusted_level;

	if (!device_open(odev))
		return 0;

	frame_bytes = cras_get_format_bytes(odev->format);

	rc = odev->frames_queued(odev);
	if (rc < 0)
		return rc;
	hw_level = rc;
	adjusted_level = adjust_level(thread, hw_level);

	audio_thread_event_log_data2(atlog, AUDIO_THREAD_FILL_AUDIO,
				     hw_level, adjusted_level);

	delay = odev->delay_frames(odev);
	if (delay < 0)
		return delay;

	/* Account for the dsp delay in addition to the hardware delay. */
	delay += get_dsp_delay(odev);

	/* Request data from streams that need more, and don't request more
	 * then hardware can hold. */
	fr_to_req = thread->buffer_frames[CRAS_STREAM_OUTPUT] - hw_level;
	if (fr_to_req > odev->buffer_size - hw_level)
			fr_to_req = odev->buffer_size - hw_level;

	rc = fetch_and_set_timestamp(thread, adjusted_level, delay);
	if (rc < 0)
		return rc;

	/* Have to loop writing to the device, will be at most 2 loops, this
	 * only happens when the circular buffer is at the end and returns us a
	 * partial area to write to from mmap_begin */
	while (total_written < fr_to_req) {
		frames = fr_to_req - total_written;
		rc = odev->get_buffer(odev, &dst, &frames);
		if (rc < 0)
			return rc;

		written = write_streams(thread,
					dst,
					adjusted_level + total_written,
					frames);
		if (written < 0) /* pcm has been closed */
			return (int)written;

		if (written < (snd_pcm_sframes_t)frames)
			/* Got all the samples from client that we can, but it
			 * won't fill the request. */
			fr_to_req = 0; /* break out after committing samples */

		loopback_iodev_add_audio(loop_dev, dst, written);

		if (cras_system_get_mute())
			memset(dst, 0, written * frame_bytes);
		else
			apply_dsp(odev, dst, written);

		if (cras_iodev_software_volume_needed(odev)) {
			cras_scale_buffer((int16_t *)dst,
					  written * odev->format->num_channels,
					  odev->software_volume_scaler);
		}

		rc = odev->put_buffer(odev, written);
		if (rc < 0)
			return rc;
		total_written += written;
	}

	/* If we haven't started the device and wrote samples, then start it. */
	if (total_written || hw_level)
		if (!odev->dev_running(odev))
			return -1;

	rc = remove_empty_streams(thread);
	if (rc < 0)
		return rc;

	/* We may close the device in remove_empty_streams() if there
	 * is no output streams. In that case, no need to get the sleep
	 * frames here. */
	if (device_open(odev)) {
		rc = get_output_sleep_frames(thread);
		if (rc < 0)
			return rc;
		*next_sleep_frames = rc;
	}

	audio_thread_event_log_data(atlog, AUDIO_THREAD_FILL_AUDIO_DONE,
				    total_written);

	return total_written;
}

/* Gets the minimum amount of space availalbe for writing across all streams of
 * the given direction (input or loopback).
 * Args:
 *    thread - The audio thread this is running in.
 *    write_limit - The initial (unlimited) number of frames to write.
 *    direction - Can be input or loopback.
 */
static unsigned int get_write_limit_set_delay(struct audio_thread *thread,
					      unsigned int write_limit,
					      int direction)
{
	struct cras_rstream *rstream;
	struct cras_audio_shm *shm;
	struct cras_audio_shm *output_shm;
	struct cras_io_stream *stream;
	unsigned int avail_frames;
	int delay;

	DL_FOREACH(thread->streams, stream) {
		rstream = stream->stream;

		if (!input_stream_matches_dev(direction, rstream))
			continue;

		delay = input_delay_frames(thread->active_devs[direction]);

		shm = cras_rstream_input_shm(rstream);
		cras_shm_check_write_overrun(shm);
		if (cras_shm_frames_written(shm) == 0)
			cras_iodev_set_capture_timestamp(
					rstream->format.frame_rate,
					delay,
					&shm->area->ts);
		cras_shm_get_writeable_frames(
				shm,
				cras_rstream_get_cb_threshold(rstream),
				&avail_frames);
		write_limit = MIN(write_limit, avail_frames);

		output_shm = cras_rstream_output_shm(rstream);
		if (output_shm->area)
			cras_iodev_set_playback_timestamp(
					rstream->format.frame_rate,
					delay,
					&output_shm->area->ts);
	}

	return write_limit;
}

/* Read samples from an input device to the specified stream.
 * Args:
 *    stream_buffer - The buffer to capture samples to.
 *    idev - The device to capture samples from.
 *    count - The number of frames to capture.
 *    dev_index - The index of the device being read from, only used to special
 *      case the first read.
 * Returns the number of frames captured or an error.
 */
static int capture_to_stream(uint8_t *stream_buffer,
			     struct cras_iodev *idev,
			     unsigned int frame_bytes,
			     unsigned int count,
			     unsigned int dev_index)
{
	snd_pcm_uframes_t remainder = count;
	unsigned int nread;
	uint8_t *hw_buffer;
	int rc;
	int offset = 0;
	int dev_has_dsp;

	dev_has_dsp = has_dsp(idev);
	while (remainder > 0) {
		nread = remainder;
		if (dev_has_dsp && nread > DSP_PROCESS_BUF_BYTES / frame_bytes)
			nread = DSP_PROCESS_BUF_BYTES / frame_bytes;

		rc = idev->get_buffer(idev, &hw_buffer, &nread);
		if (rc < 0 || nread == 0)
			return rc;

		if (dev_index == 0) {
			memcpy(stream_buffer + offset, hw_buffer,
			       nread * frame_bytes);
		} else if (dev_has_dsp) {
			memcpy(dsp_process_buf + offset, hw_buffer,
			       nread * frame_bytes);
		} else {
			cras_mix_add_clip((int16_t *)stream_buffer + offset,
					  (int16_t *)hw_buffer,
					  nread * idev->format->num_channels);
		}
		offset += nread * frame_bytes;

		rc = idev->put_buffer(idev, nread);
		if (rc < 0)
			return rc;
		remainder -= nread;
	}

	/* DSP the samples that were read. */
	if (dev_index == 0) {
		apply_dsp(idev, stream_buffer, nread);
	} else if (dev_has_dsp) {
		apply_dsp(idev, dsp_process_buf, nread);
		cras_mix_add_clip((int16_t *)stream_buffer,
				  (int16_t *)dsp_process_buf,
				  nread * idev->format->num_channels);
	}

	return count;
}

/* Transfer samples to clients from the audio device.
 * Return the number of samples read from the device.
 * Args:
 *    thread - the audio thread this is running for.
 *    idev - the device to read samples from.
 *    min_sleep - Will be filled with the minimum amount of frames needed to
 *      fill the next lowest latency stream's buffer.
 */
int possibly_read_audio(struct audio_thread *thread,
			struct cras_iodev *idev,
			unsigned int *min_sleep)
{
	struct cras_audio_shm *shm;
	struct cras_audio_shm *first_shm;
	struct cras_io_stream *stream;
	struct cras_io_stream *first_stream = NULL;
	struct active_dev *adev;
	int rc;
	unsigned int write_limit;
	unsigned int hw_level;
	unsigned int frame_bytes;
	unsigned int dev_index = 0;
	uint8_t *dst, *first_buffer;

	if (!idev || !thread->devs_open[idev->direction])
		return 0;

	frame_bytes = cras_get_format_bytes(idev->format);

	rc = input_min_frames_queued(thread->active_devs[idev->direction]);
	if (rc < 0)
		return rc;
	hw_level = rc;
	write_limit = hw_level;

	audio_thread_event_log_data(atlog, AUDIO_THREAD_READ_AUDIO, hw_level);

	/* Check if the device is still running. */
	if (!devices_running(thread, CRAS_STREAM_INPUT))
		return -1;

	write_limit = get_write_limit_set_delay(thread,
						write_limit,
						idev->direction);

	/* Read samples from this input device to the first stream. */
	DL_FOREACH(thread->streams, stream) {
		if (input_stream_matches_dev(idev->direction, stream->stream)) {
			first_stream = stream;
			break;
		}
	}

	/* Save starting write point as a bookmark to copy from later. */
	first_shm = cras_rstream_input_shm(first_stream->stream);
	first_buffer = cras_shm_get_writeable_frames(
			first_shm, cras_shm_used_frames(first_shm), NULL);

	DL_FOREACH(thread->active_devs[idev->direction], adev) {
		rc = capture_to_stream(first_buffer, adev->dev,
				       cras_shm_frame_bytes(first_shm),
				       write_limit, dev_index++);
		if (rc != write_limit)
			return rc;
	}
	if (cras_system_get_capture_mute())
		memset(first_buffer, 0, write_limit *frame_bytes);
	cras_shm_buffer_written(first_shm, write_limit);

	/* Copy the samples to other streams. */
	DL_FOREACH(thread->streams, stream) {
		struct cras_rstream *rstream = stream->stream;

		if (stream == first_stream ||
		    !input_stream_matches_dev(idev->direction, rstream))
			continue;

		shm = cras_rstream_input_shm(rstream);
		dst = cras_shm_get_writeable_frames(
				shm, cras_shm_used_frames(shm), NULL);
		memcpy(dst, first_buffer, write_limit * frame_bytes);
		cras_shm_buffer_written(shm, write_limit);
	}

	/* Minimum sleep frames should not be larger than the used callback
	 * threshold in frames for given direction.
	 */
	*min_sleep = thread->cb_threshold[idev->direction];

	DL_FOREACH(thread->streams, stream) {
		struct cras_rstream *rstream;
		unsigned int cb_threshold;

		rstream = stream->stream;

		if (!input_stream_matches_dev(idev->direction, rstream))
			continue;

		shm = cras_rstream_input_shm(rstream);
		cb_threshold = cras_rstream_get_cb_threshold(rstream);

		/* If this stream doesn't have enough data yet, skip it. */
		if (cras_shm_frames_written(shm) < cb_threshold) {
			unsigned int needed =
				cb_threshold - cras_shm_frames_written(shm);
			*min_sleep = MIN(*min_sleep, needed);
			continue;
		}

		/* Enough data for this stream, sleep until ready again. */
		*min_sleep = MIN(*min_sleep, cb_threshold);

		cras_shm_buffer_write_complete(shm);

		/* Tell the client that samples are ready. */
		rc = cras_rstream_audio_ready(
			rstream, cras_rstream_get_cb_threshold(rstream));
		if (rc < 0) {
			thread_remove_stream(thread, rstream);
			return rc;
		}

		/* Unified streams will write audio while handling the captured
		 * samples, mark them as pending. */
		if (rstream->direction == CRAS_STREAM_UNIFIED)
			cras_shm_set_callback_pending(
				cras_rstream_output_shm(rstream), 1);
	}

	if (idev->direction == CRAS_STREAM_POST_MIX_PRE_DSP) {
		/* No hardware clock here to correct for, just sleep for another
		 * period.
		 */
		*min_sleep = thread->cb_threshold[idev->direction];
	} else {
		*min_sleep = cras_iodev_sleep_frames(idev->direction,
				*min_sleep,
				hw_level - write_limit) +
			CAP_REMAINING_FRAMES_TARGET;
	}

	audio_thread_event_log_data(atlog, AUDIO_THREAD_READ_AUDIO_DONE,
				    write_limit);

	return write_limit;
}

/* Reads and/or writes audio sampels from/to the devices. */
int unified_io(struct audio_thread *thread, struct timespec *ts)
{
	struct cras_iodev *odev = first_output_dev(thread);
	struct cras_iodev *idev = first_input_dev(thread);
	struct cras_iodev *loopdev = first_loop_dev(thread);
	int rc;
	unsigned int cap_sleep_frames, pb_sleep_frames, loop_sleep_frames;
	struct timespec cap_ts, pb_ts, loop_ts;
	struct timespec *sleep_ts = NULL;

	ts->tv_sec = 0;
	ts->tv_nsec = 0;

	/* Loopback streams, filling with zeros if no output playing. */
	if (!thread->devs_open[CRAS_STREAM_OUTPUT] &&
	    thread->devs_open[CRAS_STREAM_POST_MIX_PRE_DSP]) {
		loopback_iodev_add_zeros(loopdev,
			thread->cb_threshold[CRAS_STREAM_POST_MIX_PRE_DSP]);
		loop_sleep_frames =
			thread->cb_threshold[CRAS_STREAM_POST_MIX_PRE_DSP];
	}
	possibly_read_audio(thread, loopdev, &loop_sleep_frames);

	/* Capture streams. */
	rc = possibly_read_audio(thread, idev, &cap_sleep_frames);
	if (rc < 0) {
		syslog(LOG_ERR, "read audio failed from audio thread");
		close_devices(thread, CRAS_STREAM_INPUT);
		return rc;
	}

	/* Output streams. */
	rc = possibly_fill_audio(thread, &pb_sleep_frames);
	if (rc < 0) {
		syslog(LOG_ERR, "write audio failed from audio thread");
		close_devices(thread, CRAS_STREAM_OUTPUT);
		return rc;
	}

	/* Determine which device, if any are open, needs to wake up next. */
	if (!thread->devs_open[CRAS_STREAM_INPUT] &&
	    !thread->devs_open[CRAS_STREAM_OUTPUT] &&
	    !thread->devs_open[CRAS_STREAM_POST_MIX_PRE_DSP])
		return 0;

	/* Determine which device, if any are open, needs to wake up next. */
	if (thread->devs_open[CRAS_STREAM_INPUT]) {
		cras_iodev_fill_time_from_frames(cap_sleep_frames,
						 idev->format->frame_rate,
						 &cap_ts);
		sleep_ts = &cap_ts;
		audio_thread_event_log_data(atlog, AUDIO_THREAD_INPUT_SLEEP,
					    sleep_ts->tv_nsec);
	}

	if (thread->devs_open[CRAS_STREAM_OUTPUT]) {
		cras_iodev_fill_time_from_frames(pb_sleep_frames,
						 odev->format->frame_rate,
						 &pb_ts);
		if (!sleep_ts || timespec_after(sleep_ts, &pb_ts))
			sleep_ts = &pb_ts;
		audio_thread_event_log_data(atlog, AUDIO_THREAD_OUTPUT_SLEEP,
					    sleep_ts->tv_nsec);
	}

	if (thread->devs_open[CRAS_STREAM_POST_MIX_PRE_DSP]) {
		cras_iodev_fill_time_from_frames(loop_sleep_frames,
						 loopdev->format->frame_rate,
						 &loop_ts);
		if (!sleep_ts || timespec_after(sleep_ts, &loop_ts))
			sleep_ts = &loop_ts;
		audio_thread_event_log_data(atlog, AUDIO_THREAD_LOOP_SLEEP,
					    sleep_ts->tv_nsec);
	}

	*ts = *sleep_ts;

	return 0;
}

/* For playback, fill the audio buffer when needed, for capture, pull out
 * samples when they are ready.
 * This thread will attempt to run at a high priority to allow for low latency
 * streams.  This thread sleeps while the device plays back or captures audio,
 * it will wake up as little as it can while avoiding xruns.  It can also be
 * woken by sending it a message using the "audio_thread_post_message" function.
 */
static void *audio_io_thread(void *arg)
{
	struct audio_thread *thread = (struct audio_thread *)arg;
	struct timespec ts;
	fd_set poll_set;
	int msg_fd;
	int err;

	msg_fd = thread->to_thread_fds[0];

	/* Attempt to get realtime scheduling */
	if (cras_set_rt_scheduling(CRAS_SERVER_RT_THREAD_PRIORITY) == 0)
		cras_set_thread_priority(CRAS_SERVER_RT_THREAD_PRIORITY);

	while (1) {
		struct timespec *wait_ts;
		struct iodev_callback_list *iodev_cb;
		int max_fd = msg_fd;

		wait_ts = NULL;

		if (streams_attached(thread)) {
			/* device opened */
			err = unified_io(thread, &ts);
			if (err < 0)
				syslog(LOG_ERR, "audio cb error %d", err);
			wait_ts = &ts;
		}

		FD_ZERO(&poll_set);
		FD_SET(msg_fd, &poll_set);

		DL_FOREACH(iodev_callbacks, iodev_cb) {
			FD_SET(iodev_cb->fd, &poll_set);
			if (iodev_cb->fd > max_fd)
				max_fd = iodev_cb->fd;
		}

		audio_thread_event_log_tag(atlog, AUDIO_THREAD_SLEEP);
		err = pselect(max_fd + 1, &poll_set, NULL, NULL, wait_ts, NULL);
		audio_thread_event_log_tag(atlog, AUDIO_THREAD_WAKE);
		if (err <= 0)
			continue;

		if (FD_ISSET(msg_fd, &poll_set)) {
			err = handle_playback_thread_message(thread);
			if (err < 0)
				syslog(LOG_INFO, "handle message %d", err);
		}

		DL_FOREACH(iodev_callbacks, iodev_cb)
			iodev_cb->cb(iodev_cb->cb_data, &ts,
				     FD_ISSET(iodev_cb->fd, &poll_set));
	}

	return NULL;
}

/* Write a message to the playback thread and wait for an ack, This keeps these
 * operations synchronous for the main server thread.  For instance when the
 * RM_STREAM message is sent, the stream can be deleted after the function
 * returns.  Making this synchronous also allows the thread to return an error
 * code that can be handled by the caller.
 * Args:
 *    thread - thread to receive message.
 *    msg - The message to send.
 * Returns:
 *    A return code from the message handler in the thread.
 */
static int audio_thread_post_message(struct audio_thread *thread,
				     struct audio_thread_msg *msg)
{
	int rc, err;

	err = write(thread->to_thread_fds[1], msg, msg->length);
	if (err < 0) {
		syslog(LOG_ERR, "Failed to post message to thread.");
		return err;
	}
	/* Synchronous action, wait for response. */
	err = read(thread->to_main_fds[0], &rc, sizeof(rc));
	if (err < 0) {
		syslog(LOG_ERR, "Failed to read reply from thread.");
		return err;
	}

	return rc;
}

/* Remove all streams from the thread.
 * Args:
 *    thread - a pointer to the audio thread.
 */
static void audio_thread_rm_all_streams(struct audio_thread *thread,
					enum CRAS_STREAM_DIRECTION dir)
{
	struct audio_thread_add_rm_stream_msg msg;

	assert(thread);

	msg.header.id = AUDIO_THREAD_RM_ALL_STREAMS;
	msg.header.length = sizeof(struct audio_thread_add_rm_stream_msg);
	msg.dir = dir;
	audio_thread_post_message(thread, &msg.header);
}

/* Handles metrics log message and send stats to UMA. */
static int audio_thread_metrics_log(struct audio_thread_msg *msg)
{
	static const int timeout_min_msec = 1;
	static const int timeout_max_msec = 10000;
	static const int timeout_nbuckets = 10;

	struct audio_thread_metrics_log_msg *amsg;
	amsg = (struct audio_thread_metrics_log_msg *)msg;
	switch (amsg->type) {
	case LONGEST_TIMEOUT_MSECS:
		/* Logs the longest timeout period of a stream
		 * in milliseconds. */
		syslog(LOG_INFO, "Stream longest timeout lasts %d msecs",
		       amsg->arg);
		cras_metrics_log_histogram(kStreamTimeoutMilliSeconds,
					   amsg->arg,
					   timeout_min_msec,
					   timeout_max_msec,
					   timeout_nbuckets);
		break;
	default:
		break;
	}

	return 0;
}

/* Exported Interface */

int audio_thread_add_stream(struct audio_thread *thread,
			    struct cras_rstream *stream)
{
	struct audio_thread_add_rm_stream_msg msg;

	assert(thread && stream);

	if (!thread->started)
		return -EINVAL;

	msg.header.id = AUDIO_THREAD_ADD_STREAM;
	msg.header.length = sizeof(struct audio_thread_add_rm_stream_msg);
	msg.stream = stream;
	return audio_thread_post_message(thread, &msg.header);
}

int audio_thread_disconnect_stream(struct audio_thread *thread,
				   struct cras_rstream *stream)
{
	struct audio_thread_add_rm_stream_msg msg;

	assert(thread && stream);

	msg.header.id = AUDIO_THREAD_DISCONNECT_STREAM;
	msg.header.length = sizeof(struct audio_thread_add_rm_stream_msg);
	msg.stream = stream;
	return audio_thread_post_message(thread, &msg.header);
}

int audio_thread_dump_thread_info(struct audio_thread *thread,
				  struct audio_debug_info *info)
{
	struct audio_thread_dump_debug_info_msg msg;

	msg.header.id = AUDIO_THREAD_DUMP_THREAD_INFO;
	msg.header.length = sizeof(msg);
	msg.info = info;
	return audio_thread_post_message(thread, &msg.header);
}

/* Process all kinds of queued messages post from audio thread. Called by
 * main thread.
 * Args:
 *    thread - pointer to the audio thread.
 */
static void audio_thread_process_messages(void *arg)
{
	static int max_len = 256;
	uint8_t buf[256];
	struct audio_thread_msg *msg = (struct audio_thread_msg *)buf;
	struct audio_thread *thread = arg;
	struct timespec ts = {0, 0};
	fd_set poll_set;
	int to_read, nread, err;

	while (1) {
		FD_ZERO(&poll_set);
		FD_SET(thread->main_msg_fds[0], &poll_set);
		err = pselect(thread->main_msg_fds[0] + 1, &poll_set,
			      NULL, NULL, &ts, NULL);
		if (err < 0) {
			if (err == -EINTR)
				continue;
			return;
		}

		if (!FD_ISSET(thread->main_msg_fds[0], &poll_set))
			break;

		/* Get the length of the message first */
		nread = read(thread->main_msg_fds[0], buf, sizeof(msg->length));
		if (nread < 0)
			return;
		if (msg->length > max_len)
			return;

		/* Read the rest of the message. */
		to_read = msg->length - nread;
		err = read(thread->main_msg_fds[0], &buf[0] + nread, to_read);
		if (err < 0)
			return;

		switch (msg->id) {
		case AUDIO_THREAD_METRICS_LOG:
			audio_thread_metrics_log(msg);
			break;
		default:
			syslog(LOG_ERR, "Unexpected message id %u", msg->id);
			break;
		}
	}
}

struct audio_thread *audio_thread_create()
{
	int rc;
	struct audio_thread *thread;

	thread = calloc(1, sizeof(*thread));
	if (!thread)
		return NULL;

	thread->to_thread_fds[0] = -1;
	thread->to_thread_fds[1] = -1;
	thread->to_main_fds[0] = -1;
	thread->to_main_fds[1] = -1;
	thread->main_msg_fds[0] = -1;
	thread->main_msg_fds[1] = -1;

	/* Two way pipes for communication with the device's audio thread. */
	rc = pipe(thread->to_thread_fds);
	if (rc < 0) {
		syslog(LOG_ERR, "Failed to pipe");
		free(thread);
		return NULL;
	}
	rc = pipe(thread->to_main_fds);
	if (rc < 0) {
		syslog(LOG_ERR, "Failed to pipe");
		free(thread);
		return NULL;
	}
	rc = pipe(thread->main_msg_fds);
	if (rc < 0) {
		syslog(LOG_ERR, "Failed to pipe");
		free(thread);
		return NULL;
	}

	atlog = audio_thread_event_log_init();

	cras_system_add_select_fd(thread->main_msg_fds[0],
				  audio_thread_process_messages,
				  thread);

	return thread;
}

int audio_thread_set_active_dev(struct audio_thread *thread,
				struct cras_iodev *dev)
{
	struct audio_thread_active_device_msg msg;

	assert(thread && dev);
	if (!thread->started)
		return -EINVAL;

	msg.header.id = AUDIO_THREAD_SET_ACTIVE_DEV;
	msg.header.length = sizeof(struct audio_thread_active_device_msg);
	msg.dev = dev;
	return audio_thread_post_message(thread, &msg.header);
}


int audio_thread_add_active_dev(struct audio_thread *thread,
				struct cras_iodev *dev)
{
	struct audio_thread_active_device_msg msg;

	assert(thread && dev);
	if (!thread->started)
		return -EINVAL;

	msg.header.id = AUDIO_THREAD_ADD_ACTIVE_DEV;
	msg.header.length = sizeof(struct audio_thread_active_device_msg);
	msg.dev = dev;
	return audio_thread_post_message(thread, &msg.header);
}

int audio_thread_rm_active_dev(struct audio_thread *thread,
			       struct cras_iodev *dev)
{
	struct audio_thread_active_device_msg msg;

	assert(thread && dev);
	if (!thread->started)
		return -EINVAL;

	msg.header.id = AUDIO_THREAD_RM_ACTIVE_DEV;
	msg.header.length = sizeof(struct audio_thread_active_device_msg);
	msg.dev = dev;
	return audio_thread_post_message(thread, &msg.header);
}

int audio_thread_start(struct audio_thread *thread)
{
	int rc;

	rc = pthread_create(&thread->tid, NULL, audio_io_thread, thread);
	if (rc) {
		syslog(LOG_ERR, "Failed pthread_create");
		return rc;
	}

	thread->started = 1;

	return 0;
}

void audio_thread_remove_streams(struct audio_thread *thread,
				 enum CRAS_STREAM_DIRECTION dir)
{
	if (thread->started)
		audio_thread_rm_all_streams(thread, dir);
}

void audio_thread_destroy(struct audio_thread *thread)
{
	audio_thread_event_log_deinit(atlog);

	if (thread->started) {
		struct audio_thread_msg msg;

		msg.id = AUDIO_THREAD_STOP;
		msg.length = sizeof(msg);
		audio_thread_post_message(thread, &msg);
		pthread_join(thread->tid, NULL);
	}

	thread_clear_active_devs(thread, CRAS_STREAM_OUTPUT);
	thread_clear_active_devs(thread, CRAS_STREAM_INPUT);
	thread_clear_active_devs(thread, CRAS_STREAM_POST_MIX_PRE_DSP);

	if (thread->to_thread_fds[0] != -1) {
		close(thread->to_thread_fds[0]);
		close(thread->to_thread_fds[1]);
	}
	if (thread->to_main_fds[0] != -1) {
		close(thread->to_main_fds[0]);
		close(thread->to_main_fds[1]);
	}

	cras_system_rm_select_fd(thread->main_msg_fds[0]);
	if (thread->main_msg_fds[0] != -1) {
		close(thread->main_msg_fds[0]);
		close(thread->main_msg_fds[1]);
	}

	free(thread);
}

void audio_thread_add_loopback_device(struct audio_thread *thread,
				      struct cras_iodev *loop_dev)
{
	switch (loop_dev->direction) {
	case CRAS_STREAM_POST_MIX_PRE_DSP:
		audio_thread_add_active_dev(thread, loop_dev);
		break;
	default:
		return;
	}
}
