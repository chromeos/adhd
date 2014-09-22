/* Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <pthread.h>
#include <sys/param.h>
#include <syslog.h>

#include "cras_audio_area.h"
#include "audio_thread_log.h"
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
#include "dev_stream.h"
#include "audio_thread.h"
#include "utlist.h"

#define MIN_PROCESS_TIME_US 500 /* 0.5ms - min amount of time to mix/src. */
#define SLEEP_FUZZ_FRAMES 10 /* # to consider "close enough" to sleep frames. */
#define MIN_READ_WAIT_US 2000 /* 2ms */
static const struct timespec playback_wake_fuzz_ts = {
	0, 500 * 1000 /* 500 usec. */
};

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

/* Audio thread logging. */
struct audio_thread_event_log *atlog;

/* forward declarations */
static int thread_remove_stream(struct audio_thread *thread,
				struct cras_rstream *stream);

static struct iodev_callback_list *iodev_callbacks;

struct iodev_callback_list {
	int fd;
	int is_write;
	int enabled;
	thread_callback cb;
	void *cb_data;
	struct iodev_callback_list *prev, *next;
};

static void _audio_thread_add_callback(int fd, thread_callback cb,
				       void *data, int is_write)
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
	iodev_cb->enabled = 1;
	iodev_cb->is_write = is_write;

	DL_APPEND(iodev_callbacks, iodev_cb);
}

void audio_thread_add_callback(int fd, thread_callback cb,
				void *data)
{
	_audio_thread_add_callback(fd, cb, data, 0);
}

void audio_thread_add_write_callback(int fd, thread_callback cb,
				     void *data)
{
	_audio_thread_add_callback(fd, cb, data, 1);
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

void audio_thread_enable_callback(int fd, int enabled)
{
	struct iodev_callback_list *iodev_cb;

	DL_FOREACH(iodev_callbacks, iodev_cb) {
		if (iodev_cb->fd == fd) {
			iodev_cb->enabled = !!enabled;
			return;
		}
	}
}

static inline int streams_attached_direction(const struct audio_thread *thread,
					     enum CRAS_STREAM_DIRECTION dir)
{
	struct active_dev *adev;

	DL_FOREACH(thread->active_devs[dir], adev) {
		if (adev->streams)
			return 1;
	}

	return 0;
}

/* Returns true if there are streams attached to the thread. */
static inline int streams_attached(const struct audio_thread *thread)
{
	int dir;

	for (dir = 0; dir < CRAS_NUM_DIRECTIONS; dir++) {
		if (streams_attached_direction(thread,
					       (enum CRAS_STREAM_DIRECTION)dir))
			return 1;
	}

	return 0;
}

static inline int device_open(const struct cras_iodev *iodev)
{
	if (iodev && iodev->is_open(iodev))
		return 1;
	return 0;
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

/* Find a given stream that is attached to the thread. */
static struct dev_stream *thread_find_stream(struct audio_thread *thread,
						 struct cras_rstream *stream)
{
	struct dev_stream *out;
	struct active_dev *adev_list, *adev;

	adev_list = thread->active_devs[stream->direction];

	/* Check that we don't already have this stream */
	DL_FOREACH(adev_list, adev) {
		DL_SEARCH_SCALAR(adev->streams, out, stream, stream);
		if (out)
			return out;
	}

	return NULL;
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

/* Requests audio from a stream and marks it as pending. */
static int fetch_stream(struct dev_stream *dev_stream,
			unsigned int frames_in_buff)
{
	struct cras_rstream *rstream = dev_stream->stream;
	struct cras_audio_shm *shm = cras_rstream_output_shm(rstream);
	int rc;

	audio_thread_event_log_data(
			atlog,
			AUDIO_THREAD_FETCH_STREAM,
			rstream->stream_id,
			cras_rstream_get_cb_threshold(rstream), 0);
	rc = dev_stream_request_playback_samples(dev_stream);
	if (rc < 0)
		return rc;

	update_stream_timeout(shm);
	cras_shm_clear_first_timeout(shm);

	return 0;
}

static int append_stream_to_dev(struct active_dev *adev,
				struct cras_rstream *stream)
{
	struct dev_stream *out;
	struct cras_audio_format fmt;
	struct cras_iodev *dev = adev->dev;

	if (dev->format == NULL) {
		fmt = stream->format;
		cras_iodev_set_format(dev, &fmt);
	}
	out = dev_stream_create(stream, dev->info.idx, dev->format);
	if (!out)
		return -EINVAL;

	DL_APPEND(adev->streams, out);

	return 0;
}

static int append_stream(struct audio_thread *thread,
			 struct cras_rstream *stream)
{
	struct active_dev *adev;
	unsigned int max_level = 0;

	/* Check that we don't already have this stream */
	if (thread_find_stream(thread, stream))
		return -EEXIST;

	/* TODO(dgreid) - add to correct dev, not all. */
	DL_FOREACH(thread->active_devs[stream->direction], adev) {
		int rc;
		unsigned int hw_level;

		rc = append_stream_to_dev(adev, stream);
		if (rc)
			return rc;

		if (device_open(adev->dev)) {
			hw_level = adev->dev->frames_queued(adev->dev);

			if (hw_level > max_level)
				max_level = hw_level;
		}
	}

	if (stream_uses_output(stream) && max_level < stream->cb_threshold) {
		struct cras_audio_shm *shm = cras_rstream_output_shm(stream);
		cras_shm_buffer_written(shm, stream->cb_threshold - max_level);
		cras_shm_buffer_write_complete(shm);
	}

	return 0;
}

static int delete_stream(struct audio_thread *thread,
			 struct cras_rstream *stream)
{
	struct dev_stream *out;
	int longest_timeout_msec;
	struct cras_audio_shm *shm;
	struct active_dev *adev;

	/* Find stream, and if found, delete it. */
	out = thread_find_stream(thread, stream);
	if (out == NULL)
		return -EINVAL;

	/* Log the longest timeout of the stream about to be removed. */
	if (stream_uses_output(stream)) {
		shm = cras_rstream_output_shm(stream);
		longest_timeout_msec = cras_shm_get_longest_timeout(shm);
		if (longest_timeout_msec)
			audio_thread_log_longest_timeout(
				thread, longest_timeout_msec);
	}

	/* remove from each device it is attached to. */
	DL_FOREACH(thread->active_devs[stream->direction], adev) {
		DL_FOREACH(adev->streams, out) {
			if (out->stream == stream) {
				DL_DELETE(adev->streams, out);
				dev_stream_destroy(out);
			}
		}
	}

	if (stream->client == NULL)
		cras_rstream_destroy(stream);

	return 0;
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
		if (!device_open(adev->dev))
			continue;
		rc = close_device(adev->dev);
		if (rc)
			err = rc;
	}
	return err;
}

/* Put 'frames' worth of zero samples into odev. */
int fill_odev_zeros(struct cras_iodev *odev, unsigned int frames)
{
	struct cras_audio_area *area;
	unsigned int frame_bytes, frames_written;
	int rc;

	frame_bytes = cras_get_format_bytes(odev->format);
	while (frames > 0) {
		frames_written = frames;
		rc = odev->get_buffer(odev, &area, &frames_written);
		if (rc < 0) {
			syslog(LOG_ERR, "fill zeros fail: %d", rc);
			return rc;
		}
		/* This assumes consecutive channel areas. */
		memset(area->channels[0].buf, 0,
		       frames_written * frame_bytes);
		odev->put_buffer(odev, frames_written);
		frames -= frames_written;
	}

	return 0;
}

/* Builds an initial buffer to avoid an underrun. Adds min_level of latency. */
void fill_odevs_zeros_min_level(struct cras_iodev *dev)
{
	fill_odev_zeros(dev, dev->min_buffer_level);
}

/* Open the device potentially filling the output with a pre buffer. */
static int init_device(struct cras_iodev *dev)
{
	int rc;

	if (device_open(dev))
		return 0;

	rc = dev->open_dev(dev);
	if (rc < 0)
		return rc;

	/*
	 * Start output devices by padding the output. This avoids a burst of
	 * audio callbacks when the stream starts
	 */
	if (dev->direction == CRAS_STREAM_OUTPUT)
		fill_odevs_zeros_min_level(dev);

	return 0;
}

static void thread_rm_active_dev(struct audio_thread *thread,
			  struct cras_iodev *iodev);

/* Initialize active devices of given direction. */
static int init_devices(struct audio_thread *thread,
			enum CRAS_STREAM_DIRECTION dir)
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
	rc = init_device(first_dev);
	if (rc)
		return rc;

	/* Initialize all other active devices of the same direction. */
	adevs = adevs->next;
	DL_FOREACH(adevs, adev) {
		rc = init_device(adev->dev);
		if (rc) {
			syslog(LOG_ERR, "Failed to open %s",
					adev->dev->info.name);
			thread_rm_active_dev(thread, adev->dev);
			continue;
		}
		/* If the supported format does not match what was used for
		 * selected device, close and remove it from the active device
		 * list.
		 * However we exclude input stream for this check to allow
		 * capture only a subset of channels.
		 */
		if ((dir != CRAS_STREAM_INPUT) &&
		    cras_fmt_conversion_needed(first_dev->format,
					       adev->dev->format)) {
			close_device(adev->dev);
			thread_rm_active_dev(thread, adev->dev);
		}
	}
	return 0;
}

/*
 * Non-static functions of prefix 'thread_' runs in audio thread
 * to manipulate iodevs and streams, visible for unittest.
 */

/* Clears all active devices for given direction, used to handle
 * message from main thread to set a new active device.
 */
static void thread_clear_active_devs(struct audio_thread *thread,
				     enum CRAS_STREAM_DIRECTION dir)
{
	struct active_dev *adev;

	DL_FOREACH(thread->active_devs[dir], adev) {
		if (device_open(adev->dev))
			close_device(adev->dev);
		DL_DELETE(thread->active_devs[dir], adev);
		adev->dev->is_active = 0;
		free(adev);
	}
}

/* Handles messages from main thread to set an active device. The purpose
 * of this function is to set one as the main active device, which
 * is the device at the head of the active device list.
 */
static void thread_set_active_dev(struct audio_thread *thread,
				  struct cras_iodev *iodev)
{
	struct active_dev *adev;
	struct cras_iodev *first_dev;

	first_dev = first_active_device(thread, iodev->direction);
	if (first_dev == iodev)
		return;

	/* Output device could still be in draining mode, close it. */
	close_devices(thread, iodev->direction);


	/* Remove the old first active device. */
	if (first_dev)
		thread_rm_active_dev(thread, first_dev);

	/* Look up if iodev is already in active dev list, if so then
	 * pull it out and set to the head later.
	 */
	DL_FOREACH(thread->active_devs[iodev->direction], adev) {
		if (adev->dev == iodev) {
			DL_DELETE(thread->active_devs[iodev->direction], adev);
			break;
		}
	}

	if (!adev) {
		/* Set to the first active device in list. */
		adev = (struct active_dev *)calloc(1, sizeof(*adev));
		adev->dev = iodev;
		iodev->is_active = 1;
	}

	DL_PREPEND(thread->active_devs[iodev->direction], adev);
}

/* Handles messages from main thread to add a new active device. */
static void thread_add_active_dev(struct audio_thread *thread,
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
	/* TODO(dgreid) - move some streams to this device. */
}

/* Handles messages from the main thread to remove an active device. */
static void thread_rm_active_dev(struct audio_thread *thread,
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

/* Mark or unmark all output devices' is_draining flags. */
static void devices_set_output_draining(struct audio_thread *thread,
					int is_draining) {
	struct active_dev *adev;
	DL_FOREACH(thread->active_devs[CRAS_STREAM_OUTPUT], adev) {
		adev->dev->is_draining = is_draining;
		adev->dev->extra_silent_frames = 0;
	}
}

/* Remove stream from the audio thread. If this is the last stream to be
 * removed close the device. Returns the number of streams still attached
 * to the thread.
 */
static int thread_remove_stream(struct audio_thread *thread,
				struct cras_rstream *stream)
{
	if (delete_stream(thread, stream))
		syslog(LOG_ERR, "Stream to remove not found.");

	audio_thread_event_log_data(atlog,
				    AUDIO_THREAD_STREAM_REMOVED,
				    stream->stream_id, 0, 0);

	/* When removing the last stream of a certain direction, close
	 * all active devices of that direction. Otherwise reconfigure
	 * active devices for the min latency stream.
	 */
	if (!streams_attached_direction(thread, CRAS_STREAM_INPUT))
		close_devices(thread, CRAS_STREAM_INPUT);

	if (!streams_attached_direction(thread, CRAS_STREAM_OUTPUT))
		devices_set_output_draining(thread, 1);

	if (!streams_attached_direction(thread, CRAS_STREAM_POST_MIX_PRE_DSP))
		close_devices(thread, CRAS_STREAM_POST_MIX_PRE_DSP);

	return streams_attached(thread);
}


/* Handles the disconnect_stream message from the main thread. */
static int thread_disconnect_stream(struct audio_thread* thread,
				    struct cras_rstream* stream) {
	struct dev_stream *out;

	stream->client = NULL;
	cras_rstream_set_audio_fd(stream, -1);
	cras_rstream_set_is_draining(stream, 1);

	/* If the stream has been removed from active streams, destroy it. */
	out = thread_find_stream(thread, stream);
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

/* Handles the add_stream message from the main thread. */
static int thread_add_stream(struct audio_thread *thread,
			     struct cras_rstream *stream)
{
	int rc;

	rc = append_stream(thread, stream);
	if (rc < 0)
		return AUDIO_THREAD_ERROR_OTHER;

	if (stream_uses_output(stream)) {
		/* Stream added, exit draining mode. */
		devices_set_output_draining(thread, 0);
	}

	/* If not already, open the device(s). */
	if (stream_uses_output(stream)) {
		rc = init_devices(thread, CRAS_STREAM_OUTPUT);
		if (rc < 0) {
			delete_stream(thread, stream);
			return AUDIO_THREAD_OUTPUT_DEV_ERROR;
		}
	}
	if (stream_uses_input(stream)) {
		rc = init_devices(thread, CRAS_STREAM_INPUT);
		if (rc < 0) {
			delete_stream(thread, stream);
			return AUDIO_THREAD_INPUT_DEV_ERROR;
		}
	}
	if (stream_uses_loopback(stream)) {
		rc = init_devices(thread, CRAS_STREAM_POST_MIX_PRE_DSP);
		if (rc < 0) {
			delete_stream(thread, stream);
			return AUDIO_THREAD_LOOPBACK_DEV_ERROR;
		}
	}

	audio_thread_event_log_data(atlog,
				    AUDIO_THREAD_STREAM_ADDED,
				    stream->stream_id, 0, 0);
	return 0;
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

/* Asks any stream with room for more data. Sets the time stamp for all streams.
 * Args:
 *    thread - The thread to fetch samples for.
 *    adev - The output device streams are attached to.
 * Returns:
 *    0 on success, negative error on failure. If failed, can assume that all
 *    streams have been removed from the device.
 */
static int fetch_streams(struct audio_thread *thread,
			 struct active_dev *adev)
{
	struct dev_stream *dev_stream;
	struct cras_iodev *odev = adev->dev;
	int frames_in_buff;
	int rc;
	int delay;

	delay = odev->delay_frames(odev);
	if (delay < 0)
		return delay;
	delay += get_dsp_delay(odev);

	DL_FOREACH(adev->streams, dev_stream) {
		struct cras_rstream *rstream = dev_stream->stream;
		struct cras_audio_shm *shm =
			cras_rstream_output_shm(rstream);
		int fd = cras_rstream_get_audio_fd(rstream);
		struct timespec now;

		if (cras_shm_callback_pending(shm) && fd >= 0)
			flush_old_aud_messages(shm, fd);

		frames_in_buff = cras_shm_get_frames(shm);
		if (frames_in_buff < 0)
			return frames_in_buff;

		/* Remove the stream after it is fully drained. */
		if (cras_rstream_get_is_draining(rstream)) {
			if (frames_in_buff == 0)
				thread_remove_stream(thread, rstream);
			continue;
		}

		/* Check if it's time to get more data from this stream.
		 * Allowing for waking up half a little early. */
		clock_gettime(CLOCK_MONOTONIC, &now);
		add_timespecs(&now, &playback_wake_fuzz_ts);
		if (!timespec_after(&now, dev_stream_next_cb_ts(dev_stream)))
			continue;

		dev_stream_set_delay(dev_stream, delay);

		rc = fetch_stream(dev_stream, frames_in_buff);
		if (rc < 0) {
			syslog(LOG_ERR, "fetch err: %d for %x",
			       rc, rstream->stream_id);
			/* Remove the stream if empty, otherwise drain it. */
			if (frames_in_buff == 0)
				thread_remove_stream(thread, rstream);
			else
				cras_rstream_set_is_draining(rstream, 1);
		}
	}

	return 0;
}

#if 0 //TODO(dgreid) - remove this, replace with check if haven't gotten data for a long time.
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
#endif

/* Fill the buffer with samples from the attached streams.
 * Args:
 *    adev - The device to write to.
 *    dst - The buffer to put the samples in (returned from snd_pcm_mmap_begin)
 *    write_limit - The maximum number of frames to write to dst.
 *
 * Returns:
 *    The number of frames rendered on success, a negative error code otherwise.
 *    This number of frames is the minimum of the amount of frames each stream
 *    could provide which is the maximum that can currently be rendered.
 */
static int write_streams(struct active_dev *adev,
			 uint8_t *dst,
			 size_t write_limit)
{
	struct cras_iodev *odev = adev->dev;
	struct dev_stream *curr;
	size_t num_mixed;
	unsigned int drain_limit = write_limit;
	unsigned int num_playing = 0;

	num_mixed = 0;

	/* Mix as much as we can, the minimum fill level of any stream. */
	DL_FOREACH(adev->streams, curr) {
		struct cras_audio_shm *shm;
		int shm_frames;

		shm = cras_rstream_output_shm(curr->stream);

		shm_frames = cras_shm_get_frames(shm);
		if (shm_frames < 0) /* TODO(dgreid) remove this stream. */
			continue;
		audio_thread_event_log_data(atlog,
					    AUDIO_THREAD_WRITE_STREAMS_STREAM,
					    curr->stream->stream_id,
					    shm_frames,
					    cras_shm_callback_pending(shm));
		/* If not in underrun, use this stream. */
		if (cras_rstream_get_is_draining(curr->stream)) {
			drain_limit = MIN((size_t)shm_frames, drain_limit);
		} else {
			write_limit = MIN((size_t)shm_frames, write_limit);
			num_playing++;
		}
	}

	/* Don't limit the amount written for draining streams unless there are
	 * only draining streams. */
	if (!num_playing)
		write_limit = drain_limit;

	audio_thread_event_log_data(atlog, AUDIO_THREAD_WRITE_STREAMS_MIX,
				    write_limit, 0, 0);

	DL_FOREACH(adev->streams, curr)
		dev_stream_mix(curr, odev->format->num_channels, dst,
			       &write_limit, &num_mixed);

	audio_thread_event_log_data(atlog, AUDIO_THREAD_WRITE_STREAMS_MIXED,
				    write_limit, num_mixed, 0);
	if (num_mixed == 0)
		return num_mixed;

	return write_limit;
}

/* Gets the max delay frames of active input devices. */
static int input_delay_frames(struct active_dev *adevs)
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

/* Stop the playback thread */
static void terminate_pb_thread()
{
	pthread_exit(0);
}

/* Put stream info for the given stream into the info struct. */
static void append_stream_dump_info(struct audio_debug_info *info,
				    struct dev_stream *stream,
				    int index)
{
	struct cras_audio_shm *shm;
	struct audio_stream_debug_info *si;

	shm = stream_uses_output(stream->stream) ?
		cras_rstream_output_shm(stream->stream) :
		cras_rstream_input_shm(stream->stream);

	si = &info->streams[index];

	si->stream_id = stream->stream->stream_id;
	si->direction = stream->stream->direction;
	si->buffer_frames = stream->stream->buffer_frames;
	si->cb_threshold = stream->stream->cb_threshold;
	si->frame_rate = stream->stream->format.frame_rate;
	si->num_channels = stream->stream->format.num_channels;
	si->num_cb_timeouts = cras_shm_num_cb_timeouts(shm);
	memcpy(si->channel_layout, stream->stream->format.channel_layout,
	       sizeof(si->channel_layout));
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
			amsg->stream->stream_id, 0, 0);
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
		struct dev_stream *iostream;
		struct audio_thread_add_rm_stream_msg *rmsg;
		enum CRAS_STREAM_DIRECTION dir;

		rmsg = (struct audio_thread_add_rm_stream_msg *)msg;
		dir = rmsg->dir;

		/* For each stream; detach and tell client to reconfig. */
		/* TODO(dgreid) - handle > 1 active iodev, even needed?  */
		DL_FOREACH(thread->active_devs[dir]->streams, iostream) {
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
		thread_set_active_dev(thread, dev);
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
		struct dev_stream *curr;
		struct cras_iodev *idev = first_input_dev(thread);
		struct cras_iodev *odev = first_output_dev(thread);
		struct audio_thread_dump_debug_info_msg *dmsg;
		struct audio_debug_info *info;
		unsigned int num_streams = 0;

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

		/* TODO(dgreid) - handle > 1 active iodev */
		DL_FOREACH(thread->active_devs[CRAS_STREAM_OUTPUT]->streams,
			   curr) {
			append_stream_dump_info(info, curr, num_streams);
			if (++num_streams == MAX_DEBUG_STREAMS)
				break;
		}
		DL_FOREACH(thread->active_devs[CRAS_STREAM_INPUT]->streams,
			   curr) {
			if (num_streams == MAX_DEBUG_STREAMS)
				break;
			append_stream_dump_info(info, curr, num_streams);
			++num_streams;
		}
		info->num_streams = num_streams;

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

/* Fills the time that the next stream needs to be serviced. */
static void get_next_stream_wake_from_list(struct dev_stream *streams,
					   struct timespec *min_ts)
{
	struct dev_stream *dev_stream;

	DL_FOREACH(streams, dev_stream) {
		const struct timespec *next_cb_ts;
		if (cras_rstream_get_is_draining(dev_stream->stream))
			continue;

		next_cb_ts = dev_stream_next_cb_ts(dev_stream);
		audio_thread_event_log_data(atlog,
					    AUDIO_THREAD_STREAM_SLEEP_TIME,
					    dev_stream->stream->stream_id,
					    next_cb_ts->tv_sec,
					    next_cb_ts->tv_nsec);
		if (timespec_after(min_ts, next_cb_ts))
			*min_ts = *next_cb_ts;
	}
}

static void get_next_stream_wake(struct audio_thread *thread,
				 struct timespec *min_ts,
				 const struct timespec *now)
{
	struct active_dev *adev;

	DL_FOREACH(thread->active_devs[CRAS_STREAM_OUTPUT], adev)
		get_next_stream_wake_from_list(adev->streams, min_ts);
	DL_FOREACH(thread->active_devs[CRAS_STREAM_INPUT], adev)
		get_next_stream_wake_from_list(adev->streams, min_ts);
	DL_FOREACH(thread->active_devs[CRAS_STREAM_POST_MIX_PRE_DSP], adev)
		get_next_stream_wake_from_list(adev->streams, min_ts);
}

static void get_next_dev_wake(struct audio_thread *thread,
			      struct timespec *min_ts,
			      const struct timespec *now)
{
	struct active_dev *adev;

	DL_FOREACH(thread->active_devs[CRAS_STREAM_OUTPUT], adev) {
		/* Only wake up for devices when they finish draining. */
		if (!device_open(adev->dev) || !adev->dev->is_draining)
			continue;
		audio_thread_event_log_data(atlog,
					    AUDIO_THREAD_DEV_SLEEP_TIME,
					    adev->dev->info.idx,
					    adev->wake_ts.tv_sec,
					    adev->wake_ts.tv_nsec);
		if (timespec_after(min_ts, &adev->wake_ts))
			*min_ts = adev->wake_ts;
	}
}

/* Drain the hardware buffer of odev.
 * Args:
 *    odev - the output device to be drainned.
 */
int drain_output_buffer(struct cras_iodev *odev)
{
	int hw_level;
	int filled_count;
	int buffer_frames;
	int rc;

	buffer_frames = odev->buffer_size;

	hw_level = odev->frames_queued(odev);
	if (hw_level < 0)
		return hw_level;

	if ((int)odev->extra_silent_frames >= hw_level) {
		/* Remaining audio has been played out. Close the device. */
		close_device(odev);
		odev->is_draining = 0;
		return 0;
	}

	filled_count = MIN(buffer_frames - hw_level,
			   2048 - (int)odev->extra_silent_frames);
	rc = fill_odev_zeros(odev, filled_count);
	if (rc)
		return rc;
	odev->extra_silent_frames += filled_count;

	return 0;
}

static void set_odev_wake_times(struct active_dev *dev_list)
{
	struct active_dev *adev;
	struct timespec now;

	clock_gettime(CLOCK_MONOTONIC, &now);

	DL_FOREACH(dev_list, adev) {
		struct timespec sleep_time;
		int hw_level;

		if (!device_open(adev->dev))
			continue;

		hw_level = adev->dev->frames_queued(adev->dev);
		if (hw_level < 0)
			return;

		audio_thread_event_log_data(atlog,
					    AUDIO_THREAD_SET_DEV_WAKE,
					    adev->dev->info.idx,
					    0, 0);

		cras_frames_to_time(hw_level, adev->dev->format->frame_rate,
				    &sleep_time);
		adev->wake_ts = now;
		add_timespecs(&adev->wake_ts, &sleep_time);
	}
}

static int output_stream_fetch(struct audio_thread *thread)
{
	struct active_dev *odev_list = thread->active_devs[CRAS_STREAM_OUTPUT];
	struct active_dev *adev;

	DL_FOREACH(odev_list, adev) {
		if (!device_open(adev->dev))
			continue;
		fetch_streams(thread, adev);
	}

	return 0;
}

static int wait_pending_output_streams(struct audio_thread *thread)
{
	/* TODO(dgreid) - is this needed? */
	return 0;
}

static int write_output_samples(struct active_dev *adev,
				struct cras_iodev *loop_dev)
{
	struct cras_iodev *odev = adev->dev;
	unsigned int hw_level;
	unsigned int frames, fr_to_req;
	snd_pcm_sframes_t written;
	snd_pcm_uframes_t total_written = 0;
	int rc;
	uint8_t *dst = NULL;
	struct cras_audio_area *area;

	if (odev->is_draining)
		return drain_output_buffer(odev);

	rc = odev->frames_queued(odev);
	if (rc < 0)
		return rc;
	hw_level = rc;

	audio_thread_event_log_data(atlog, AUDIO_THREAD_FILL_AUDIO,
				    hw_level, 0, 0);

	/* Don't request more than hardware can hold. */
	fr_to_req = odev->buffer_size - hw_level;

	/* Have to loop writing to the device, will be at most 2 loops, this
	 * only happens when the circular buffer is at the end and returns us a
	 * partial area to write to from mmap_begin */
	while (total_written < fr_to_req) {
		frames = fr_to_req - total_written;
		rc = odev->get_buffer(odev, &area, &frames);
		if (rc < 0)
			return rc;

		/* TODO(dgreid) - This assumes interleaved audio. */
		dst = area->channels[0].buf;
		written = write_streams(adev,
					dst,
					frames);
		if (written < 0) /* pcm has been closed */
			return (int)written;

		if (written < (snd_pcm_sframes_t)frames)
			/* Got all the samples from client that we can, but it
			 * won't fill the request. */
			fr_to_req = 0; /* break out after committing samples */

		loopback_iodev_add_audio(loop_dev, dst, written);

		if (cras_system_get_mute()) {
			unsigned int frame_bytes;
			frame_bytes = cras_get_format_bytes(odev->format);
			cras_mix_mute_buffer(dst, frame_bytes, written);
		} else {
			apply_dsp(odev, dst, written);
		}

		if (cras_iodev_software_volume_needed(odev)) {
			cras_scale_buffer((int16_t *)dst,
					  written * odev->format->num_channels,
					  cras_iodev_get_software_volume_scaler(
							odev));
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

	audio_thread_event_log_data(atlog, AUDIO_THREAD_FILL_AUDIO_DONE,
				    total_written, 0, 0);
	return 0;
}

static int do_playback(struct audio_thread *thread)
{
	struct active_dev *odev_list = thread->active_devs[CRAS_STREAM_OUTPUT];
	struct active_dev *adev;

	DL_FOREACH(odev_list, adev) {
		if (!device_open(adev->dev))
			continue;
		write_output_samples(adev, first_loop_dev(thread));
	}

	set_odev_wake_times(odev_list);

	return 0;
}

/* Gets the minimum amount of space available for writing across all streams.
 * Args:
 *    adev - The device to capture from.
 *    write_limit - Initial limit to number of frames to capture.
 */
static unsigned int get_stream_limit_set_delay(struct active_dev *adev,
					      unsigned int write_limit)
{
	struct cras_rstream *rstream;
	struct cras_audio_shm *shm;
	struct dev_stream *stream;
	int delay;

	/* TODO(dgreid) - Setting delay from last dev only. */
	delay = input_delay_frames(adev);

	DL_FOREACH(adev->streams, stream) {
		rstream = stream->stream;

		shm = cras_rstream_input_shm(rstream);
		cras_shm_check_write_overrun(shm);
		dev_stream_set_delay(stream, delay);
		write_limit = MIN(write_limit,
				  dev_stream_capture_avail(stream));
	}

	return write_limit;
}

/* Read samples from an input device to the specified stream.
 * Args:
 *    adev - The device to capture samples from.
 *    dev_index - The index of the device being read from, only used to special
 *      case the first read.
 * Returns 0 on success.
 */
static int capture_to_streams(struct active_dev *adev,
			      unsigned int dev_index)
{
	struct cras_iodev *idev = adev->dev;
	unsigned int frame_bytes = cras_get_format_bytes(idev->format);
	snd_pcm_uframes_t remainder, hw_level;

	hw_level = idev->frames_queued(idev);
	remainder = MIN(hw_level, get_stream_limit_set_delay(adev, hw_level));

	audio_thread_event_log_data(atlog, AUDIO_THREAD_READ_AUDIO,
				    idev->info.idx, hw_level, remainder);

	if (!idev->dev_running(idev))
		return 0;

	while (remainder > 0) {
		struct cras_audio_area *area;
		struct dev_stream *stream;
		uint8_t *hw_buffer;
		unsigned int nread;
		int rc;

		nread = remainder;

		rc = idev->get_buffer(idev, &area, &nread);
		if (rc < 0 || nread == 0)
			return rc;
		/* TODO(dgreid) - This assumes interleaved audio. */
		hw_buffer = area->channels[0].buf;

		if (cras_system_get_capture_mute())
			cras_mix_mute_buffer(hw_buffer, frame_bytes, nread);
		else
			apply_dsp(idev, hw_buffer, nread);

		DL_FOREACH(adev->streams, stream)
			dev_stream_capture(stream, area, dev_index);

		rc = idev->put_buffer(idev, nread);
		if (rc < 0)
			return rc;
		remainder -= nread;
	}

	audio_thread_event_log_data(atlog, AUDIO_THREAD_READ_AUDIO_DONE,
				    0, 0, 0);

	return 0;
}

static int do_capture(struct audio_thread *thread)
{
	struct active_dev *idev_list = thread->active_devs[CRAS_STREAM_INPUT];
	struct active_dev *adev;
	unsigned int dev_index = 0;

	DL_FOREACH(idev_list, adev) {
		if (!device_open(adev->dev))
			continue;
		capture_to_streams(adev, dev_index);
		dev_index++;
	}

	return 0;
}

static int send_captured_samples(struct audio_thread *thread)
{
	struct active_dev *idev_list = thread->active_devs[CRAS_STREAM_INPUT];
	struct active_dev *adev;

	// TODO(dgreid) - once per rstream, not once per dev_stream.
	DL_FOREACH(idev_list, adev) {
		struct dev_stream *stream;
		DL_FOREACH(adev->streams, stream) {
			dev_stream_capture_update_rstream(stream);
		}
	}

	return 0;
}

/* Reads and/or writes audio sampels from/to the devices. */
static int stream_dev_io(struct audio_thread *thread)
{
	output_stream_fetch(thread);
	do_capture(thread);
	send_captured_samples(thread);
	wait_pending_output_streams(thread);
	do_playback(thread);

	return 0;
}

void fill_next_sleep_interval(struct audio_thread *thread, struct timespec *ts)
{
	struct timespec min_ts;
	struct timespec now;

	ts->tv_sec = 0;
	ts->tv_nsec = 0;
	/* Limit the sleep time to 20 seconds. */
	min_ts.tv_sec = 20;
	min_ts.tv_nsec = 0;
	clock_gettime(CLOCK_MONOTONIC, &now);
	add_timespecs(&min_ts, &now);
	get_next_stream_wake(thread, &min_ts, &now);
	get_next_dev_wake(thread, &min_ts, &now);
	if (timespec_after(&min_ts, &now))
		subtract_timespecs(&min_ts, &now, ts);
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
	struct active_dev *adev;
	struct dev_stream *curr;
	struct timespec ts;
	fd_set poll_set;
	fd_set poll_write_set;
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

		if (device_open(first_output_dev(thread)) ||
		    device_open(first_input_dev(thread)) ||
		    device_open(first_loop_dev(thread))) {
			/* device opened */
			err = stream_dev_io(thread);
			if (err < 0)
				syslog(LOG_ERR, "audio cb error %d", err);
			fill_next_sleep_interval(thread, &ts);
			wait_ts = &ts;
		}

		FD_ZERO(&poll_set);
		FD_ZERO(&poll_write_set);
		FD_SET(msg_fd, &poll_set);

		DL_FOREACH(iodev_callbacks, iodev_cb) {
			if (!iodev_cb->enabled)
				continue;
			if (iodev_cb->is_write)
				FD_SET(iodev_cb->fd, &poll_write_set);
			else
				FD_SET(iodev_cb->fd, &poll_set);
			if (iodev_cb->fd > max_fd)
				max_fd = iodev_cb->fd;
		}

		/* TODO(dgreid) - once per rstream not per dev_stream */
		DL_FOREACH(thread->active_devs[CRAS_STREAM_OUTPUT], adev) {
			DL_FOREACH(adev->streams, curr) {
				if (cras_rstream_get_is_draining(curr->stream))
					continue;
				FD_SET(curr->stream->fd, &poll_set);
				if (curr->stream->fd > max_fd)
					max_fd = curr->stream->fd;
			}
		}

		audio_thread_event_log_data(atlog, AUDIO_THREAD_SLEEP,
					    wait_ts ? wait_ts->tv_sec : 0,
					    wait_ts ? wait_ts->tv_nsec : 0, 0);
		err = pselect(max_fd + 1, &poll_set, &poll_write_set, NULL,
			      wait_ts, NULL);
		audio_thread_event_log_data(atlog, AUDIO_THREAD_WAKE, 0, 0, 0);
		if (err <= 0)
			continue;

		if (FD_ISSET(msg_fd, &poll_set)) {
			err = handle_playback_thread_message(thread);
			if (err < 0)
				syslog(LOG_INFO, "handle message %d", err);
		}

		DL_FOREACH(iodev_callbacks, iodev_cb) {
			if (FD_ISSET(iodev_cb->fd, &poll_set) ||
			    FD_ISSET(iodev_cb->fd, &poll_write_set))
				iodev_cb->cb(iodev_cb->cb_data);
		}
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
	static unsigned int max_len = 256;
	uint8_t buf[256];
	struct audio_thread_msg *msg = (struct audio_thread_msg *)buf;
	struct audio_thread *thread = (struct audio_thread *)arg;
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

	thread = (struct audio_thread *)calloc(1, sizeof(*thread));
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
