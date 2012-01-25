/* Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/*
 * Messages sent between the server and clients.
 */
#ifndef CRAS_MESSAGES_H_
#define CRAS_MESSAGES_H_

#include <stdint.h>

#include "cras_types.h"

#define CRAS_PROTO_VER 0 /* Rev when message format changes. */
#define MAX_AUD_SERV_MSG_SIZE 256

/* Message IDs. */
enum {
	/* Client -> Server*/
	AUD_SERV_CLIENT_STREAM_CONNECT,
	AUD_SERV_CLIENT_STREAM_DISCONNECT,
	AUD_SERV_SWITCH_STREAM_TYPE_IODEV,
	/* Server -> Client */
	AUD_SERV_CLIENT_CONNECTED,
	AUD_SERV_CLIENT_STREAM_CONNECTED,
	AUD_SERV_CLIENT_STREAM_REATTACH,
	NUM_AUD_SERV_MESSAGES
};

/* Message "base class". */
struct cras_message {
	uint32_t length;
	uint32_t id;
};

/*
 * Messages from client to server.
 */

/* Sent by a client to connect a stream to the server. */
struct cras_connect_message {
	struct cras_message header;
	uint32_t proto_version;
	uint32_t direction; /* input or output */
	cras_stream_id_t stream_id; /* unique id for this stream */
	uint32_t stream_type; /* media, or call, etc. */
	uint32_t buffer_frames; /* Buffer size in frames. */
	uint32_t cb_threshold; /* callback client when this much is left */
	uint32_t min_cb_level; /* don't callback unless this much is avail */
	uint32_t flags;
	struct cras_audio_format format; /* rate, channels, sample size */
};
static inline void cras_fill_connect_message(struct cras_connect_message *m,
					   uint32_t direction,
					   cras_stream_id_t stream_id,
					   uint32_t stream_type,
					   uint32_t buffer_frames,
					   uint32_t cb_threshold,
					   uint32_t min_cb_level,
					   uint32_t flags,
					   struct cras_audio_format format)
{
	m->proto_version = CRAS_PROTO_VER;
	m->direction = direction;
	m->stream_id = stream_id;
	m->stream_type = stream_type;
	m->buffer_frames = buffer_frames;
	m->cb_threshold = cb_threshold;
	m->min_cb_level = min_cb_level;
	m->flags = flags;
	m->format = format;
	m->header.id = AUD_SERV_CLIENT_STREAM_CONNECT;
	m->header.length = sizeof(struct cras_connect_message);
}

/* Sent by a client to remove a stream from the server. */
struct cras_disconnect_stream_message {
	struct cras_message header;
	cras_stream_id_t stream_id;
};
static inline void cras_fill_disconnect_stream_message(
		struct cras_disconnect_stream_message *m,
		cras_stream_id_t stream_id)
{
	m->stream_id = stream_id;
	m->header.id = AUD_SERV_CLIENT_STREAM_DISCONNECT;
	m->header.length = sizeof(struct cras_disconnect_stream_message);
}

/* Move streams of "type" to the iodev at "iodev_idx". */
struct cras_switch_stream_type_iodev {
	struct cras_message header;
	uint32_t stream_type;
	uint32_t iodev_idx;
};
static inline void fill_cras_switch_stream_type_iodev(
		struct cras_switch_stream_type_iodev *m,
		uint32_t stream_type, uint32_t iodev_idx)
{
	m->stream_type = stream_type;
	m->iodev_idx = iodev_idx;
	m->header.id = AUD_SERV_SWITCH_STREAM_TYPE_IODEV;
	m->header.length = sizeof(struct cras_switch_stream_type_iodev);
}

/*
 * Messages sent from server to client.
 */

/* Reply from the server indicating that the client has connected. */
struct cras_client_connected {
	struct cras_message header;
	uint32_t client_id;
};
static inline void cras_fill_client_connected(
		struct cras_client_connected *m,
		uint32_t client_id)
{
	m->client_id = client_id;
	m->header.id = AUD_SERV_CLIENT_CONNECTED;
	m->header.length = sizeof(struct cras_client_connected);
}

/* Reply from server that a stream has been successfully added. */
struct cras_client_stream_connected {
	struct cras_message header;
	int err;
	cras_stream_id_t stream_id;
	struct cras_audio_format format;
	int shm_key;
	size_t shm_max_size;
};
static inline void cras_fill_client_stream_connected(
		struct cras_client_stream_connected *m,
		int err,
		cras_stream_id_t stream_id,
		struct cras_audio_format format,
		int shm_key,
		size_t shm_max_size)
{
	m->err = err;
	m->stream_id = stream_id;
	m->format = format;
	m->shm_key = shm_key;
	m->shm_max_size = shm_max_size;
	m->header.id = AUD_SERV_CLIENT_STREAM_CONNECTED;
	m->header.length = sizeof(struct cras_client_stream_connected);
}

/* Reattach a given stream.  This is used to indicate that a stream has been
 * removed from it's device and should be re-attached.  Occurs when moving
 * streams. */
struct cras_client_stream_reattach {
	struct cras_message header;
	cras_stream_id_t stream_id;
};
static inline void cras_fill_client_stream_reattach(
		struct cras_client_stream_reattach *m,
		cras_stream_id_t stream_id)
{
	m->stream_id = stream_id;
	m->header.id = AUD_SERV_CLIENT_STREAM_REATTACH;
	m->header.length = sizeof(struct cras_client_stream_reattach);
}

/*
 * Messages specific to passing audio between client and server
 */
enum {
	AUDIO_MESSAGE_REQUEST_DATA,
	AUDIO_MESSAGE_DATA_READY,
	NUM_AUDIO_MESSAGES
};

struct audio_message {
	uint32_t id;
	int error;
	uint32_t frames; /* number of samples per channel */
};

#endif /* CRAS_MESSAGES_H_ */
