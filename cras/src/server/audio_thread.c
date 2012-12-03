/* Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <pthread.h>
#include <syslog.h>

#include "cras_config.h"
#include "cras_dsp.h"
#include "cras_dsp_pipeline.h"
#include "cras_iodev.h"
#include "cras_mix.h"
#include "cras_rstream.h"
#include "cras_types.h"
#include "cras_util.h"
#include "audio_thread.h"
#include "utlist.h"

#define MIN_PROCESS_TIME_US 500 /* 0.5ms - min amount of time to mix/src. */
#define SLEEP_FUZZ_FRAMES 10 /* # to consider "close enough" to sleep frames. */
#define MIN_READ_WAIT_US 2000 /* 2ms */

/* Sleep a few extra frames to make sure that the samples are ready. */
static const unsigned int CAP_REMAINING_FRAMES_TARGET = 16;


/* Returns true if there are streams attached to the thread. */
static inline int streams_attached(const struct audio_thread *thread)
{
	return thread->streams != NULL;
}

/* Finds the lowest latency stream attached to the thread. */
static struct cras_io_stream *
get_min_latency_stream(const struct audio_thread *thread)
{
	struct cras_io_stream *lowest, *curr;

	lowest = thread->streams;
	DL_FOREACH(thread->streams, curr)
		if (cras_rstream_get_buffer_size(curr->stream) <
		    cras_rstream_get_buffer_size(lowest->stream))
			lowest = curr;

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
			  int *out_active)
{
	struct cras_io_stream *curr;

	*in_active = 0;
	*out_active = 0;

	DL_FOREACH(thread->streams, curr) {
		enum CRAS_STREAM_DIRECTION dir = curr->stream->direction;

		if (dir != CRAS_STREAM_OUTPUT)
			*in_active = *in_active + 1;
		if (dir != CRAS_STREAM_INPUT)
			*out_active = *out_active + 1;
	}

	return *in_active || *out_active;
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

	cras_rstream_set_thread(stream, thread);

	return 0;
}

static int delete_stream(struct audio_thread *thread,
			 struct cras_rstream *stream)
{
	struct cras_io_stream *out;

	/* Find stream, and if found, delete it. */
	DL_SEARCH_SCALAR(thread->streams, out, stream, stream);
	if (out == NULL)
		return -EINVAL;

	cras_rstream_set_thread(stream, NULL);

	DL_DELETE(thread->streams, out);
	free(out);

	return 0;
}

/* open the device configured to play the format of the given stream. */
static int init_device(struct cras_iodev *dev, struct cras_rstream *stream)
{
	struct cras_audio_format fmt;
	cras_rstream_get_format(stream, &fmt);
	cras_iodev_set_format(dev, &fmt);
	return dev->open_dev(dev);
}

/* Handles the rm_stream message from the main thread.
 * If this is the last stream to be removed close the device.
 * Returns the number of streams still attached to the thread.
 */
int thread_remove_stream(struct audio_thread *thread,
			 struct cras_rstream *stream)
{
	struct cras_iodev *odev = thread->output_dev;
	struct cras_iodev *idev = thread->input_dev;
	int in_active, out_active;

	if (delete_stream(thread, stream))
		syslog(LOG_ERR, "Stream to remove not found.");

	active_streams(thread, &in_active, &out_active);

	if (!in_active) {
		/* No more streams, close the dev. */
		if (idev && idev->is_open(idev)) {
			idev->close_dev(idev);
			thread->input_dev = NULL;
			idev->thread = NULL;
		}
	} else {
		struct cras_io_stream *min_latency;
		min_latency = get_min_latency_stream(thread);
		cras_iodev_config_params(
			idev,
			cras_rstream_get_buffer_size(min_latency->stream),
			cras_rstream_get_cb_threshold(min_latency->stream));
	}
	if (!out_active) {
		/* No more streams, close the dev. */
		if (odev && odev->is_open(odev)) {
			odev->close_dev(odev);
			thread->output_dev = NULL;
			odev->thread = NULL;
		}
	} else {
		struct cras_io_stream *min_latency;
		min_latency = get_min_latency_stream(thread);
		cras_iodev_config_params(
			odev,
			cras_rstream_get_buffer_size(min_latency->stream),
			cras_rstream_get_cb_threshold(min_latency->stream));
	}

	return streams_attached(thread);
}

/* Put 'frames' worth of zero samples into odev.  Used to build an initial
 * buffer to avoid an underrun. Adds 'frames' latency.
 */
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

/* Handles the add_stream message from the main thread. */
int thread_add_stream(struct audio_thread *thread,
		      struct cras_rstream *stream)
{
	struct cras_iodev *odev = thread->output_dev;
	struct cras_iodev *idev = thread->input_dev;
	struct cras_io_stream *min_latency;
	int rc;
	int in_active, out_active;

	active_streams(thread, &in_active, &out_active);

	/* Only allow one capture stream to attach. */
	if (in_active && (stream->direction == CRAS_STREAM_INPUT ||
			  stream->direction == CRAS_STREAM_UNIFIED))
		return -EBUSY;

	rc = append_stream(thread, stream);
	if (rc < 0)
		return rc;

	/* If not already, open the device(s). */
	if (stream_has_output(stream) && !odev->is_open(odev)) {
		thread->sleep_correction_frames = 0;
		rc = init_device(odev, stream);
		if (rc < 0)
			syslog(LOG_ERR, "Failed to open %s", odev->info.name);

		/* Give some buffer for the output for unified IO, need time to
		 * read samples and fill playback buffer before hitting
		 * underflow.
		 */
		if (odev && idev)
			fill_odev_zeros(odev, odev->cb_threshold);
	}
	if (stream_has_input(stream) && !idev->is_open(idev)) {
		thread->sleep_correction_frames = 0;
		rc = init_device(idev, stream);
		if (rc < 0)
			syslog(LOG_ERR, "Failed to open %s", idev->info.name);
	}

	min_latency = get_min_latency_stream(thread);

	if (odev)
		cras_iodev_config_params(
			odev,
			cras_rstream_get_buffer_size(min_latency->stream),
			cras_rstream_get_cb_threshold(min_latency->stream));

	if (idev)
		cras_iodev_config_params(
			idev,
			cras_rstream_get_buffer_size(min_latency->stream),
			cras_rstream_get_cb_threshold(min_latency->stream));

	return 0;
}

static void apply_dsp_pipeline(struct pipeline *pipeline, size_t channels,
			       uint8_t *buf, size_t frames)
{
	size_t remaining;
	size_t chunk;
	size_t i, j;
	int16_t *target, *target_ptr;
	float *source[channels], *sink[channels];
	float *source_ptr[channels], *sink_ptr[channels];
	struct timespec begin, end, delta;

	if (!pipeline || frames == 0)
		return;

	clock_gettime(CLOCK_THREAD_CPUTIME_ID, &begin);

	target = (int16_t *)buf;

	/* get pointers to source and sink buffers */
	for (i = 0; i < channels; i++) {
		source[i] = cras_dsp_pipeline_get_source_buffer(pipeline, i);
		sink[i] = cras_dsp_pipeline_get_sink_buffer(pipeline, i);
	}

	remaining = frames;

	/* process at most DSP_BUFFER_SIZE frames each loop */
	while (remaining > 0) {
		chunk = min(remaining, (size_t)DSP_BUFFER_SIZE);

		/* deinterleave and convert to float */
		target_ptr = target;
		for (i = 0; i < channels; i++)
			source_ptr[i] = source[i];
		for (i = 0; i < chunk; i++) {
			for (j = 0; j < channels; j++)
				*(source_ptr[j]++) = *target_ptr++ / 32768.0f;
		}

		cras_dsp_pipeline_run(pipeline, chunk);

		/* interleave and convert back to int16_t */
		target_ptr = target;
		for (i = 0; i < channels; i++)
			sink_ptr[i] = sink[i];
		for (i = 0; i < chunk; i++) {
			for (j = 0; j < channels; j++) {
				float f = *(sink_ptr[j]++) * 32768.0f;
				int16_t i16;
				if (f > 32767)
					i16 = 32767;
				else if (f < -32768)
					i16 = -32768;
				else
					i16 = (int16_t) (f + 0.5f);
				*target_ptr++ = i16;
			}
		}

		target += chunk * channels;
		remaining -= chunk;
	}

	clock_gettime(CLOCK_THREAD_CPUTIME_ID, &end);
	subtract_timespecs(&end, &begin, &delta);
	cras_dsp_pipeline_add_statistic(pipeline, &delta, frames);
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

	apply_dsp_pipeline(pipeline, iodev->format->num_channels, buf,
			   frames);

	cras_dsp_put_pipeline(ctx);
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
 *    fetch_size - How much to fetch.
 *    delay - How much latency is queued to the device (frames).
 * Returns:
 *    0 on success, negative error on failure. If failed, can assume that all
 *    streams have been removed from the device.
 */
static int fetch_and_set_timestamp(struct audio_thread *thread,
				   size_t fetch_size,
				   size_t delay)
{
	struct cras_iodev *odev = thread->output_dev;
	size_t fr_rate, frames_in_buff;
	struct cras_io_stream *curr, *tmp;
	int rc;

	fr_rate = odev->format->frame_rate;

	DL_FOREACH_SAFE(thread->streams, curr, tmp) {
		struct cras_audio_shm *shm =
			cras_rstream_output_shm(curr->stream);

		if (cras_shm_callback_pending(shm))
			flush_old_aud_messages(shm, curr->fd);

		frames_in_buff = cras_shm_get_frames(shm);

		cras_iodev_set_playback_timestamp(fr_rate,
						  frames_in_buff + delay,
						  &shm->area->ts);

		/* If we already have enough data, don't poll this stream. */
		if (frames_in_buff >= fetch_size)
			continue;

		if (!cras_shm_callback_pending(shm) &&
		    cras_shm_is_buffer_available(shm)) {
			rc = cras_rstream_request_audio(curr->stream,
							fetch_size);
			if (rc < 0) {
				thread_remove_stream(thread, curr->stream);
				/* If this failed and was the last stream,
				 * return, otherwise, on to the next one */
				if (!streams_attached(thread))
					return -EIO;
				continue;
			}
			cras_shm_set_callback_pending(shm, 1);
		}
	}

	return 0;
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
	struct cras_iodev *odev = thread->output_dev;
	struct cras_io_stream *curr, *tmp;
	struct timeval to;
	fd_set poll_set, this_set;
	size_t streams_wait, num_mixed;
	int max_fd;
	int rc;

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

		shm = cras_rstream_output_shm(curr->stream);

		curr->mixed = 0;
		if (cras_shm_get_frames(shm) < write_limit &&
		    cras_shm_callback_pending(shm)) {
			/* Not enough to mix this call, wait for a response. */
			streams_wait++;
			FD_SET(curr->fd, &poll_set);
			if (curr->fd > max_fd)
				max_fd = curr->fd;
		} else {
			curr->mixed = cras_mix_add_stream(
				shm,
				odev->format->num_channels,
				dst, &write_limit, &num_mixed);
		}
	}

	/* Wait until all polled clients reply, or a timeout. */
	while (streams_wait > 0) {
		this_set = poll_set;
		rc = select(max_fd + 1, &this_set, NULL, NULL, &to);
		if (rc <= 0) {
			/* Timeout */
			DL_FOREACH(thread->streams, curr) {
				struct cras_audio_shm *shm;

				shm = cras_rstream_output_shm(curr->stream);

				if (cras_shm_callback_pending(shm) &&
				    FD_ISSET(curr->fd, &poll_set))
					cras_shm_inc_cb_timeouts(shm);
			}
			break;
		}
		DL_FOREACH_SAFE(thread->streams, curr, tmp) {
			struct cras_audio_shm *shm;

			if (!FD_ISSET(curr->fd, &this_set))
				continue;

			shm = cras_rstream_output_shm(curr->stream);

			FD_CLR(curr->fd, &poll_set);
			streams_wait--;
			cras_shm_set_callback_pending(shm, 0);
			rc = cras_rstream_get_audio_request_reply(curr->stream);
			if (rc < 0) {
				thread_remove_stream(thread, curr->stream);
				if (!streams_attached(thread))
					return -EIO;
				continue;
			}
			if (curr->mixed)
				continue;
			curr->mixed = cras_mix_add_stream(
				shm,
				odev->format->num_channels,
				dst, &write_limit, &num_mixed);
		}
	}

	if (num_mixed == 0)
		return num_mixed;

	/* For all streams rendered, mark the data used as read. */
	DL_FOREACH(thread->streams, curr) {
		struct cras_audio_shm *shm;

		shm = cras_rstream_output_shm(curr->stream);
		if (curr->mixed)
			cras_shm_buffer_read(shm, write_limit);
	}

	return write_limit;
}

static inline int have_enough_frames(const struct cras_iodev *iodev,
				     unsigned int frames)
{
	if (iodev->direction == CRAS_STREAM_OUTPUT)
		return frames <= (iodev->cb_threshold + SLEEP_FUZZ_FRAMES);

	/* Input or unified. */
	return frames >= iodev->cb_threshold;
}

/* Pass captured samples to the client.
 * Args:
 *    thread - The thread pass read samples to.
 *    src - the memory area containing the captured samples.
 *    count - the number of frames captured = buffer_frames.
 */
static void read_streams(struct audio_thread *thread,
			 const uint8_t *src,
			 snd_pcm_uframes_t count)
{
	struct cras_io_stream *streams;
	struct cras_audio_shm *shm;
	unsigned write_limit;
	uint8_t *dst;

	streams = thread->streams;
	if (!streams)
		return; /* Nowhere to put samples. */

	shm = cras_rstream_input_shm(streams->stream);

	dst = cras_shm_get_writeable_frames(shm, &write_limit);
	count = min(count, write_limit);
	memcpy(dst, src, count * cras_shm_frame_bytes(shm));
	cras_shm_buffer_written(shm, count);
}

/* Stop the playback thread */
static int terminate_pb_thread()
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
		ret = thread_add_stream(thread, amsg->stream);
		break;
	}
	case AUDIO_THREAD_RM_STREAM: {
		struct audio_thread_add_rm_stream_msg *rmsg;

		rmsg = (struct audio_thread_add_rm_stream_msg *)msg;

		cras_rstream_log_overrun(rmsg->stream);

		ret = thread_remove_stream(thread, rmsg->stream);
		break;
	}
	case AUDIO_THREAD_STOP:
		ret = 0;
		err = audio_thread_send_response(thread, ret);
		if (err < 0)
			return err;
		terminate_pb_thread();
		break;
	default:
		ret = -EINVAL;
		break;
	}

	err = audio_thread_send_response(thread, ret);
	if (err < 0)
		return err;
	return ret;
}

/* Transfer samples from clients to the audio device.
 * Return the number of samples written to the device.
 */
int possibly_fill_audio(struct audio_thread *thread,
			unsigned int delay,
			unsigned int hw_level)
{
	unsigned int frames, fr_to_req;
	snd_pcm_sframes_t written;
	snd_pcm_uframes_t total_written = 0;
	int rc;
	uint8_t *dst = NULL;
	struct cras_iodev *odev = thread->output_dev;

	if (!odev)
		return 0;

	/* Request data from streams that need more */
	fr_to_req = odev->used_size - hw_level;
	rc = fetch_and_set_timestamp(thread, fr_to_req, delay);
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

		written = write_streams(thread, dst, hw_level + total_written,
					frames);
		if (written < 0) /* pcm has been closed */
			return (int)written;

		if (written < (snd_pcm_sframes_t)frames)
			/* Got all the samples from client that we can, but it
			 * won't fill the request. */
			fr_to_req = 0; /* break out after committing samples */

		apply_dsp(odev, dst, written);
		rc = odev->put_buffer(odev, written);
		if (rc < 0)
			return rc;
		total_written += written;
	}

	/* If we haven't started the device and wrote samples, then start it. */
	if (total_written || hw_level) {
		rc = odev->dev_running(odev);
		if (rc < 0)
			return rc;
	}

	return total_written;
}

static struct cras_rstream *find_capture_stream(struct audio_thread *thread)
{
	struct cras_io_stream *curr;

	DL_FOREACH(thread->streams, curr)
		if (curr->stream->direction == CRAS_STREAM_INPUT ||
		    curr->stream->direction == CRAS_STREAM_UNIFIED)
			return curr->stream;
	return NULL;
}

/* Transfer samples to clients from the audio device.
 * Return the number of samples read from the device.
 */
int possibly_read_audio(struct audio_thread *thread,
			unsigned int delay,
			unsigned int hw_level)
{
	snd_pcm_uframes_t remainder;
	struct cras_audio_shm *shm;
	struct cras_rstream *rstream;
	int rc;
	uint8_t *src;
	uint8_t *dst = NULL;
	unsigned int write_limit = 0;
	unsigned int nread, level_target;
	struct cras_iodev *idev = thread->input_dev;

	if (!idev)
		return 0;

	rstream = find_capture_stream(thread);
	if (rstream) {
		shm = cras_rstream_input_shm(rstream);
		cras_shm_check_write_overrun(shm);
		cras_iodev_set_capture_timestamp(idev->format->frame_rate,
						 delay,
						 &shm->area->ts);
		dst = cras_shm_get_writeable_frames(shm, &write_limit);
	}

	remainder = idev->cb_threshold;
	while (remainder > 0) {
		nread = remainder;
		rc = idev->get_buffer(idev, &src, &nread);
		if (rc < 0 || nread == 0)
			return rc;

		read_streams(thread, src, nread);

		rc = idev->put_buffer(idev, nread);
		if (rc < 0)
			return rc;
		remainder -= nread;
	}

	if (find_capture_stream(thread)) {
		apply_dsp(idev, dst, min(idev->cb_threshold, write_limit));
		shm = cras_rstream_input_shm(rstream);
		cras_shm_buffer_write_complete(shm);

		/* Tell the client that samples are ready.  This assumes only
		 * one capture client at a time. */
		rc = cras_rstream_audio_ready(rstream,
					      idev->cb_threshold);
		if (rc < 0) {
			thread_remove_stream(thread, rstream);
			return rc;
		}

		/* Unified streams will write audio while handling the captured
		 * samples, make them as pending. */
		if (rstream->direction == CRAS_STREAM_UNIFIED)
			cras_shm_set_callback_pending(shm, 1);
	}

	/* If there are more remaining frames than targeted, decrease the sleep
	 * time.  If less, increase. */
	level_target = thread->remaining_target + idev->cb_threshold;
	if (hw_level > level_target && thread->sleep_correction_frames)
		thread->sleep_correction_frames--;
	if (hw_level < level_target)
		thread->sleep_correction_frames++;

	return idev->cb_threshold;
}

/* Reads and/or writes audio sampels from/to the devices. */
int unified_io(struct audio_thread *thread, struct timespec *ts)
{
	struct cras_iodev *idev = thread->input_dev;
	struct cras_iodev *odev = thread->output_dev;
	struct cras_iodev *master_dev = idev ? : odev;
	int rc, delay;
	unsigned int hw_level, to_sleep;

	ts->tv_sec = 0;
	ts->tv_nsec = 0;

	rc = master_dev->frames_queued(master_dev);
	if (rc < 0)
		return rc;
	hw_level = rc;

	if (!have_enough_frames(master_dev, hw_level)) {
		/* Check if the pcm is still running. */
		rc = master_dev->dev_running(master_dev);
		if (rc < 0)
			return rc;
		/* Increase sleep correction factor when waking up too early. */
		thread->sleep_correction_frames++;
		goto not_enough;
	}

	rc = master_dev->delay_frames(master_dev);
	if (rc < 0)
		return rc;
	delay = rc;

	rc = possibly_read_audio(thread, delay, hw_level);
	if (rc < 0) {
		syslog(LOG_ERR, "read audio failed from audio thread");
		idev->close_dev(idev);
		return rc;
	}
	hw_level -= rc;

	if (!odev)
		goto not_enough;

	rc = odev->frames_queued(odev);
	if (rc < 0)
		return rc;

	rc = possibly_fill_audio(thread, delay, rc);
	if (rc < 0) {
		syslog(LOG_ERR, "write audio failed from audio thread");
		odev->close_dev(odev);
		return rc;
	}
	if (!idev)
		hw_level += rc;

not_enough:
	 /* idev could have been closed for error. */
	master_dev = thread->input_dev ? : thread->output_dev;

	if (!master_dev)
		return -EIO;

	to_sleep = cras_iodev_sleep_frames(master_dev, hw_level) +
		   thread->remaining_target +
		   thread->sleep_correction_frames;

	cras_iodev_fill_time_from_frames(to_sleep,
					 master_dev->format->frame_rate,
					 ts);

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

		wait_ts = NULL;

		if (streams_attached(thread)) {
			/* device opened */
			err = unified_io(thread, &ts);
			if (err < 0)
				syslog(LOG_INFO, "audio cb error %d", err);
			wait_ts = &ts;
		}

		FD_ZERO(&poll_set);
		FD_SET(msg_fd, &poll_set);
		err = pselect(msg_fd + 1, &poll_set, NULL, NULL, wait_ts, NULL);
		if (err > 0 && FD_ISSET(msg_fd, &poll_set)) {
			err = handle_playback_thread_message(thread);
			if (err < 0)
				syslog(LOG_INFO, "handle message %d", err);
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

/* Exported Interface */

int audio_thread_add_stream(struct audio_thread *thread,
			    struct cras_rstream *stream)
{
	struct audio_thread_add_rm_stream_msg msg;

	assert(thread && stream);

	msg.header.id = AUDIO_THREAD_ADD_STREAM;
	msg.header.length = sizeof(struct audio_thread_add_rm_stream_msg);
	msg.stream = stream;
	return audio_thread_post_message(thread, &msg.header);
}

int audio_thread_rm_stream(struct audio_thread *thread,
			   struct cras_rstream *stream)
{
	struct audio_thread_add_rm_stream_msg msg;

	assert(thread && stream);

	msg.header.id = AUDIO_THREAD_RM_STREAM;
	msg.header.length = sizeof(struct audio_thread_add_rm_stream_msg);
	msg.stream = stream;
	return audio_thread_post_message(thread, &msg.header);
}

void audio_thread_rm_all_streams(struct audio_thread *thread)
{
	struct cras_io_stream *iostream, *tmp;

	/* For each stream; detach and tell client to reconfig. */
	DL_FOREACH_SAFE(thread->streams, iostream, tmp) {
		cras_rstream_send_client_reattach(iostream->stream);
		audio_thread_rm_stream(thread, iostream->stream);
	}
}

struct audio_thread *audio_thread_create(struct cras_iodev *iodev)
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

	if (iodev->direction == CRAS_STREAM_INPUT) {
		thread->input_dev = iodev;
		thread->remaining_target = CAP_REMAINING_FRAMES_TARGET;
	} else {
		thread->output_dev = iodev;
		thread->remaining_target = 0;
	}

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

	iodev->thread = thread;

	return thread;
}

void audio_thread_add_output_dev(struct audio_thread *thread,
				 struct cras_iodev *odev)
{
	thread->output_dev = odev;
	odev->thread = thread;
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

void audio_thread_destroy(struct audio_thread *thread)
{
	if (thread->started) {
		struct audio_thread_msg msg;

		msg.id = AUDIO_THREAD_STOP;
		msg.length = sizeof(msg);
		audio_thread_post_message(thread, &msg);
		pthread_join(thread->tid, NULL);
	}

	if (thread->input_dev)
		thread->input_dev->thread = NULL;
	if (thread->output_dev)
		thread->output_dev->thread = NULL;

	if (thread->to_thread_fds[0] != -1) {
		close(thread->to_thread_fds[0]);
		close(thread->to_thread_fds[1]);
	}
	if (thread->to_main_fds[0] != -1) {
		close(thread->to_main_fds[0]);
		close(thread->to_main_fds[1]);
	}

	free(thread);
}
