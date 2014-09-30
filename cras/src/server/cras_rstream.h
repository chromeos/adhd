/* Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/*
 * Remote Stream - An audio steam from/to a client.
 */
#ifndef CRAS_RSTREAM_H_
#define CRAS_RSTREAM_H_

#include "cras_shm.h"
#include "cras_types.h"

struct cras_rclient;
struct dev_mix;

/* Holds identifiers for an shm segment.
 *  shm_key - Key shared with client to access shm.
 *  shm_id - Returned from shmget.
 */
struct rstream_shm_info {
	int shm_key;
	int shm_id;
};

/* cras_rstream is used to manage an active audio stream from
 * a client.  Each client can have any number of open streams for
 * playing or recording.
 * Members:
 *    stream_id - identifier for this stream.
 *    stream_type - not used.
 *    direction - input or output.
 *    fd - Socket for requesting and sending audio buffer events.
 *    buffer_frames - Buffer size in frames.
 *    cb_threshold - Callback client when this much is left.
 *    is_draining - The stream is draining and waiting to be removed.
 *    client - The client who uses this stream.
 *    shm_info - Configuration data for shared memory
 *    shm - shared memory
 *    audio_area - space for playback/capture audio
 *    format - format of the stream
 *    next_cb_ts - Next callback time for this stream.
 *    sleep_interval_ts - Time between audio callbacks.
 *    buf_state - State of the buffer from all devices for this stream.
 */
struct cras_rstream {
	cras_stream_id_t stream_id;
	enum CRAS_STREAM_TYPE stream_type;
	enum CRAS_STREAM_DIRECTION direction;
	int fd;
	size_t buffer_frames;
	size_t cb_threshold;
	int is_draining;
	struct cras_rclient *client;
	struct rstream_shm_info shm_info;
	struct cras_audio_shm shm;
	struct cras_audio_area *audio_area;
	struct cras_audio_format format;
	struct timespec next_cb_ts;
	struct timespec sleep_interval_ts;
	struct buffer_share *buf_state;
	struct cras_rstream *prev, *next;
};

/* Creates an rstream.
 * Args:
 *    stream_type - CRAS_STREAM_TYPE.
 *    direction - CRAS_STREAM_OUTPUT or CRAS_STREAM_INPUT.
 *    format - The audio format the stream wishes to use.
 *    buffer_frames - Total number of audio frames to buffer.
 *    cb_threshold - # of frames when to request more from the client.
 *    client - The client that owns this stream.
 *    stream_out - Filled with the newly created stream pointer.
 * Returns:
 *    0 on success, EINVAL if an invalid argument is passed, or ENOMEM if out of
 *    memory.
 */
int cras_rstream_create(cras_stream_id_t stream_id,
			enum CRAS_STREAM_TYPE stream_type,
			enum CRAS_STREAM_DIRECTION direction,
			const struct cras_audio_format *format,
			size_t buffer_frames,
			size_t cb_threshold,
			struct cras_rclient *client,
			struct cras_rstream **stream_out);
/* Destroys an rstream. */
void cras_rstream_destroy(struct cras_rstream *stream);

/* Gets the total buffer size in frames for the given client stream. */
static inline size_t cras_rstream_get_buffer_size(
		const struct cras_rstream *stream)
{
	return stream->buffer_frames;
}

/* Gets the callback threshold in frames for the given client stream. */
static inline size_t cras_rstream_get_cb_threshold(
		const struct cras_rstream *stream)
{
	return stream->cb_threshold;
}

/* Gets the stream type of this stream. */
static inline enum CRAS_STREAM_TYPE cras_rstream_get_type(
		const struct cras_rstream *stream)
{
	return stream->stream_type;
}

/* Gets the direction (input/output/loopback) of the stream. */
static inline enum CRAS_STREAM_DIRECTION cras_rstream_get_direction(
		const struct cras_rstream *stream)
{
	return stream->direction;
}

/* Gets the format for the stream. */
static inline void cras_rstream_set_format(struct cras_rstream *stream,
					   const struct cras_audio_format *fmt)
{
	stream->format = *fmt;
}

/* Sets the format for the stream. */
static inline int cras_rstream_get_format(const struct cras_rstream *stream,
					  struct cras_audio_format *fmt)
{
	*fmt = stream->format;
	return 0;
}

/* Gets fd to be used to poll this client for audio. */
static inline void cras_rstream_set_audio_fd(struct cras_rstream *stream,
					     int fd)
{
	stream->fd = fd;
}

/* Sets fd to be used to poll this client for audio. */
static inline int cras_rstream_get_audio_fd(const struct cras_rstream *stream)
{
	return stream->fd;
}

/* Gets the is_draning flag. */
static inline int cras_rstream_get_is_draining(struct cras_rstream *stream)
{
	return stream->is_draining;
}

/* Sets the is_draning flag. */
static inline void cras_rstream_set_is_draining(struct cras_rstream *stream,
					    int is_draining)
{
	stream->is_draining = is_draining;
}

/* Gets the shm key used to find the outputshm region. */
static inline int cras_rstream_output_shm_key(const struct cras_rstream *stream)
{
	return stream->shm_info.shm_key;
}

/* Gets the shm key used to find the input shm region. */
static inline int cras_rstream_input_shm_key(const struct cras_rstream *stream)
{
	return stream->shm_info.shm_key;
}

/* Gets the total size of shm memory allocated. */
static inline size_t cras_rstream_get_total_shm_size(
		const struct cras_rstream *stream)
{
	if (stream->direction == CRAS_STREAM_OUTPUT)
		return cras_shm_total_size(&stream->shm);

	/* Use the shm size for loopback streams. */
	return cras_shm_total_size(&stream->shm);
}

/* Gets shared memory region for this stream. */
static inline
struct cras_audio_shm *cras_rstream_input_shm(struct cras_rstream *stream)
{
	return &stream->shm;
}

/* Gets shared memory region for this stream. */
static inline
struct cras_audio_shm *cras_rstream_output_shm(struct cras_rstream *stream)
{
	return &stream->shm;
}

/* Checks if the stream uses an output device. */
static inline int stream_uses_output(const struct cras_rstream *s)
{
	return cras_stream_uses_output_hw(s->direction);
}

/* Checks if the stream uses an input device. */
static inline int stream_uses_input(const struct cras_rstream *s)
{
	return cras_stream_uses_input_hw(s->direction);
}

/* Checks if the stream uses a loopback device. */
static inline int stream_uses_loopback(const struct cras_rstream *s)
{
	return cras_stream_is_loopback(s->direction);
}

/* Requests min_req frames from the client. */
int cras_rstream_request_audio(const struct cras_rstream *stream);

/* Tells a capture client that count frames are ready. */
int cras_rstream_audio_ready(struct cras_rstream *stream, size_t count);
/* Waits for the response to a request for audio. */
int cras_rstream_get_audio_request_reply(const struct cras_rstream *stream);
/* Sends a message to the client telling him to re-attach the stream. Used when
 * moving a stream between io devices. */
void cras_rstream_send_client_reattach(const struct cras_rstream *stream);

/* Let the rstream know when a device is added or removed. */
void cras_rstream_dev_attach(struct cras_rstream *rstream, unsigned int dev_id);
void cras_rstream_dev_detach(struct cras_rstream *rstream, unsigned int dev_id);

/* A device using this stream has read or written samples. */
void cras_rstream_dev_offset_update(struct cras_rstream *rstream,
				    unsigned int frames,
				    unsigned int dev_id);

void cras_rstream_update_input_write_pointer(struct cras_rstream *rstream);
void cras_rstream_update_output_read_pointer(struct cras_rstream *rstream);

unsigned int cras_rstream_dev_offset(const struct cras_rstream *rstream,
				     unsigned int dev_id);

static inline int cras_rstream_input_level_met(struct cras_rstream *rstream)
{
	const struct cras_audio_shm *shm = cras_rstream_input_shm(rstream);
	return cras_shm_frames_written(shm) >= rstream->cb_threshold;
}

/* Returns the number of playable samples in shm for the given device id. */
unsigned int cras_rstream_playable_frames(struct cras_rstream *rstream,
					  unsigned int dev_id);

#endif /* CRAS_RSTREAM_H_ */
