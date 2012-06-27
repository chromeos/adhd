/* Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
#include <stdint.h>
#include <sys/shm.h>
#include <syslog.h>

#include "cras_config.h"
#include "cras_messages.h"
#include "cras_rclient.h"
#include "cras_rstream.h"
#include "cras_shm.h"
#include "cras_types.h"


/* Setup the shared memory area used to pass audio samples. */
static int cras_rstream_setup_shm(struct cras_rstream *stream)
{
	size_t used_size, samples_size, total_size, frame_bytes;
	int loops = 0;
	const struct cras_audio_format *fmt = &stream->format;

	if (stream->shm != NULL) /* already setup */
		return -EEXIST;

	frame_bytes = snd_pcm_format_physical_width(fmt->format) / 8 *
			fmt->num_channels;
	used_size = stream->buffer_frames * frame_bytes;
	samples_size = used_size * CRAS_NUM_SHM_BUFFERS;
	total_size = sizeof(struct cras_audio_shm_area) + samples_size;

	/* Find an available shm key. */
	do {
		stream->shm_key = getpid() + stream->stream_id + loops;
		stream->shm_id = shmget(stream->shm_key, total_size,
					IPC_CREAT | IPC_EXCL | 0660);
	} while (stream->shm_id < 0 && loops++ < 100);
	if (stream->shm_id < 0) {
		syslog(LOG_ERR, "shmget");
		return stream->shm_id;
	}

	/* Attach to shm and clear it. */
	stream->shm = shmat(stream->shm_id, NULL, 0);
	if (stream->shm == (void *) -1)
		return -ENOMEM;
	memset(stream->shm, 0, total_size);
	stream->shm->frame_bytes = frame_bytes;
	stream->shm->size = samples_size;
	stream->shm->used_size = used_size;
	stream->shm->volume_scaler = 1.0;
	return 0;
}

/* Verifies that the given stream parameters are valid. */
static int verify_rstream_parameters(enum CRAS_STREAM_DIRECTION direction,
				     const struct cras_audio_format *format,
				     size_t buffer_frames,
				     size_t cb_threshold,
				     size_t min_cb_level,
				     struct cras_rclient *client,
				     struct cras_rstream **stream_out)
{
	if (buffer_frames < CRAS_MIN_BUFFER_SIZE_FRAMES) {
		syslog(LOG_ERR, "rstream: invalid buffer_frames %zu\n",
		       buffer_frames);
		return -EINVAL;
	}
	if (stream_out == NULL) {
		syslog(LOG_ERR, "rstream: stream_out can't be NULL\n");
		return -EINVAL;
	}
	if (format == NULL) {
		syslog(LOG_ERR, "rstream: format can't be NULL\n");
		return -EINVAL;
	}
	if ((format->format != SND_PCM_FORMAT_S16_LE) &&
	    (format->format != SND_PCM_FORMAT_S32_LE) &&
	    (format->format != SND_PCM_FORMAT_U8) &&
	    (format->format != SND_PCM_FORMAT_S24_LE)) {
		syslog(LOG_ERR, "rstream: format %d not supported\n",
		       format->format);
		return -EINVAL;
	}
	if (direction != CRAS_STREAM_OUTPUT && direction != CRAS_STREAM_INPUT) {
		syslog(LOG_ERR, "rstream: Invalid direction.\n");
		return -EINVAL;
	}
	if (cb_threshold < CRAS_MIN_BUFFER_SIZE_FRAMES) {
		syslog(LOG_ERR, "rstream: cb_threshold too low\n");
		return -EINVAL;
	}
	if (min_cb_level < CRAS_MIN_BUFFER_SIZE_FRAMES) {
		syslog(LOG_ERR, "rstream: min_cb_level too low\n");
		return -EINVAL;
	}
	return 0;
}

/* Exported functions */

int cras_rstream_create(cras_stream_id_t stream_id,
			enum CRAS_STREAM_TYPE stream_type,
			enum CRAS_STREAM_DIRECTION direction,
			const struct cras_audio_format *format,
			size_t buffer_frames,
			size_t cb_threshold,
			size_t min_cb_level,
			uint32_t flags,
			struct cras_rclient *client,
			struct cras_rstream **stream_out)
{
	struct cras_rstream *stream;
	int rc;

	rc = verify_rstream_parameters(direction, format, buffer_frames,
				       cb_threshold, min_cb_level, client,
				       stream_out);
	if (rc < 0)
		return rc;

	stream = calloc(1, sizeof(*stream));
	if (stream == NULL)
		return -ENOMEM;

	stream->stream_id = stream_id;
	stream->stream_type = stream_type;
	stream->direction = direction;
	stream->format = *format;
	stream->buffer_frames = buffer_frames;
	stream->cb_threshold = cb_threshold;
	stream->min_cb_level = min_cb_level;
	stream->flags = flags;
	stream->client = client;
	stream->shm = NULL;

	rc = cras_rstream_setup_shm(stream);
	if (rc < 0) {
		syslog(LOG_ERR, "failed to setup shm %d\n", rc);
		free(stream);
		return rc;
	}

	syslog(LOG_DEBUG, "stream %x frames %zu, cb_thresh %zu, shm_size %zu",
	       stream_id, buffer_frames, cb_threshold, stream->shm->size);
	*stream_out = stream;
	return 0;
}

void cras_rstream_destroy(struct cras_rstream *stream)
{
	if (stream->shm != NULL) {
		shmdt(stream->shm);
		shmctl(stream->shm_id, IPC_RMID, (void *)stream->shm);
	}
	free(stream);
}

int cras_rstream_request_audio(const struct cras_rstream *stream, size_t count)
{
	struct audio_message msg;
	int rc;

	if (count < stream->min_cb_level)
		count = stream->min_cb_level;
	msg.id = AUDIO_MESSAGE_REQUEST_DATA;
	msg.frames = count;
	rc = write(stream->fd, &msg, sizeof(msg));
	return rc;
}

int cras_rstream_audio_ready(const struct cras_rstream *stream, size_t count)
{
	struct audio_message msg;
	int rc;

	msg.id = AUDIO_MESSAGE_DATA_READY;
	msg.frames = count;
	rc = write(stream->fd, &msg, sizeof(msg));
	return rc;
}

int cras_rstream_get_audio_request_reply(const struct cras_rstream *stream)
{
	struct audio_message msg;
	int rc;

	rc = read(stream->fd, &msg, sizeof(msg));
	if (rc < 0 || msg.error < 0)
		return -EIO;
	return 0;
}

int cras_rstream_request_audio_buffer(const struct cras_rstream *stream)
{
	return cras_rstream_request_audio(stream, stream->buffer_frames);
}

void cras_rstream_send_client_reattach(const struct cras_rstream *stream)
{
	struct cras_client_stream_reattach msg;
	cras_fill_client_stream_reattach(&msg, stream->stream_id);
	cras_rclient_send_message(stream->client, &msg.header);
}
