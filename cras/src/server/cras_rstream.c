/* Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
#include <stdint.h>
#include <sys/shm.h>
#include <syslog.h>

#include "cras_audio_area.h"
#include "cras_config.h"
#include "cras_messages.h"
#include "cras_rclient.h"
#include "cras_rstream.h"
#include "cras_shm.h"
#include "cras_types.h"
#include "buffer_share.h"

/* Configure the shm area for the stream. */
static int setup_shm(struct cras_rstream *stream,
		     struct cras_audio_shm *shm,
		     struct rstream_shm_info *shm_info)
{
	size_t used_size, samples_size, total_size, frame_bytes;
	int loops = 0;
	const struct cras_audio_format *fmt = &stream->format;

	if (shm->area != NULL) /* already setup */
		return -EEXIST;

	frame_bytes = snd_pcm_format_physical_width(fmt->format) / 8 *
			fmt->num_channels;
	used_size = stream->buffer_frames * frame_bytes;
	samples_size = used_size * CRAS_NUM_SHM_BUFFERS;
	total_size = sizeof(struct cras_audio_shm_area) + samples_size;

	/* Find an available shm key. */
	do {
		shm_info->shm_key = getpid() + stream->stream_id + loops;
		shm_info->shm_id = shmget(shm_info->shm_key,
					  total_size,
					  IPC_CREAT | IPC_EXCL | 0660);
	} while (shm_info->shm_id < 0 && loops++ < 100);
	if (shm_info->shm_id < 0) {
		syslog(LOG_ERR, "shmget");
		return shm_info->shm_id;
	}

	/* Attach to shm and clear it. */
	shm->area = shmat(shm_info->shm_id, NULL, 0);
	if (shm->area == (void *)-1)
		return -ENOMEM;
	memset(shm->area, 0, total_size);
	cras_shm_set_volume_scaler(shm, 1.0);
	/* Set up config and copy to shared area. */
	cras_shm_set_frame_bytes(shm, frame_bytes);
	shm->config.frame_bytes = frame_bytes;
	cras_shm_set_used_size(shm, used_size);
	memcpy(&shm->area->config, &shm->config, sizeof(shm->config));
	return 0;
}

/* Setup the shared memory area used for audio samples. */
static inline int setup_shm_area(struct cras_rstream *stream)
{
	int rc;

	rc = setup_shm(stream, &stream->shm,
			&stream->shm_info);
	if (rc)
		return rc;
	stream->audio_area =
		cras_audio_area_create(stream->format.num_channels);
	cras_audio_area_config_channels(stream->audio_area, &stream->format);

	return 0;
}

static inline int buffer_meets_size_limit(size_t buffer_size, size_t rate)
{
	return buffer_size > (CRAS_MIN_BUFFER_TIME_IN_US * rate) / 1000000;
}

/* Verifies that the given stream parameters are valid. */
static int verify_rstream_parameters(enum CRAS_STREAM_DIRECTION direction,
				     const struct cras_audio_format *format,
				     size_t buffer_frames,
				     size_t cb_threshold,
				     struct cras_rclient *client,
				     struct cras_rstream **stream_out)
{
	if (!buffer_meets_size_limit(buffer_frames, format->frame_rate)) {
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
	if (direction != CRAS_STREAM_OUTPUT &&
	    direction != CRAS_STREAM_INPUT &&
	    direction != CRAS_STREAM_POST_MIX_PRE_DSP) {
		syslog(LOG_ERR, "rstream: Invalid direction.\n");
		return -EINVAL;
	}
	if (!buffer_meets_size_limit(cb_threshold, format->frame_rate)) {
		syslog(LOG_ERR, "rstream: cb_threshold too low\n");
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
			struct cras_rclient *client,
			struct cras_rstream **stream_out)
{
	struct cras_rstream *stream;
	int rc;

	rc = verify_rstream_parameters(direction, format, buffer_frames,
				       cb_threshold, client,
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
	stream->client = client;
	stream->shm.area = NULL;

	rc = setup_shm_area(stream);
	if (rc < 0) {
		syslog(LOG_ERR, "failed to setup shm %d\n", rc);
		free(stream);
		return rc;
	}

	stream->buf_state = buffer_share_create(stream->buffer_frames);

	syslog(LOG_DEBUG, "stream %x frames %zu, cb_thresh %zu",
	       stream_id, buffer_frames, cb_threshold);
	*stream_out = stream;
	return 0;
}

void cras_rstream_destroy(struct cras_rstream *stream)
{
	if (stream->shm.area != NULL) {
		shmdt(stream->shm.area);
		shmctl(stream->shm_info.shm_id, IPC_RMID,
		       (void *)stream->shm.area);
		cras_audio_area_destroy(stream->audio_area);
	}
	buffer_share_destroy(stream->buf_state);
	free(stream);
}

int cras_rstream_request_audio(const struct cras_rstream *stream)
{
	struct audio_message msg;
	int rc;

	/* Only request samples from output streams. */
	if (stream->direction != CRAS_STREAM_OUTPUT)
		return 0;

	msg.id = AUDIO_MESSAGE_REQUEST_DATA;
	msg.frames = stream->cb_threshold;
	rc = write(stream->fd, &msg, sizeof(msg));
	return rc;
}

int cras_rstream_audio_ready(struct cras_rstream *stream, size_t count)
{
	struct audio_message msg;
	int rc;

	cras_shm_buffer_write_complete(&stream->shm);

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

void cras_rstream_send_client_reattach(const struct cras_rstream *stream)
{
	struct cras_client_stream_reattach msg;
	if (!stream->client)
		return;
	cras_fill_client_stream_reattach(&msg, stream->stream_id);
	cras_rclient_send_message(stream->client, &msg.header);
}

void cras_rstream_dev_attach(struct cras_rstream *rstream, unsigned int dev_id)
{
	buffer_share_add_dev(rstream->buf_state, dev_id);
}

void cras_rstream_dev_detach(struct cras_rstream *rstream, unsigned int dev_id)
{
	buffer_share_rm_dev(rstream->buf_state, dev_id);
}

void cras_rstream_dev_offset_update(struct cras_rstream *rstream,
				    unsigned int frames,
				    unsigned int dev_id)
{
	buffer_share_offset_update(rstream->buf_state, dev_id, frames);
}

void cras_rstream_update_input_write_pointer(struct cras_rstream *rstream)
{
	struct cras_audio_shm *shm = cras_rstream_input_shm(rstream);
	unsigned int nwritten = buffer_share_get_new_write_point(
					rstream->buf_state);

	cras_shm_buffer_written(shm, nwritten);
}

void cras_rstream_update_output_read_pointer(struct cras_rstream *rstream)
{
	struct cras_audio_shm *shm = cras_rstream_input_shm(rstream);
	unsigned int nwritten = buffer_share_get_new_write_point(
					rstream->buf_state);

	cras_shm_buffer_read(shm, nwritten);
}

unsigned int cras_rstream_dev_offset(const struct cras_rstream *rstream,
				     unsigned int dev_id)
{
	return buffer_share_dev_offset(rstream->buf_state, dev_id);
}

unsigned int cras_rstream_playable_frames(struct cras_rstream *rstream,
					  unsigned int dev_id)
{
	const struct cras_audio_shm *shm = cras_rstream_output_shm(rstream);

	return cras_shm_get_frames(shm) -
			cras_rstream_dev_offset(rstream, dev_id);
}
