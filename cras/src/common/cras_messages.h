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

#include "cras_iodev_info.h"
#include "cras_types.h"

/* Rev when message format changes. If new messages are added, or message ID
 * values change. */
#define CRAS_PROTO_VER 0
#define CRAS_SERV_MAX_MSG_SIZE 256

/* Message IDs. */
enum CRAS_SERVER_MESSAGE_ID {
	/* Client -> Server*/
	CRAS_SERVER_CONNECT_STREAM,
	CRAS_SERVER_DISCONNECT_STREAM,
	CRAS_SERVER_SWITCH_STREAM_TYPE_IODEV,
	CRAS_SERVER_SET_SYSTEM_VOLUME,
	CRAS_SERVER_SET_SYSTEM_MUTE,
	CRAS_SERVER_SET_SYSTEM_CAPTURE_GAIN,
	CRAS_SERVER_SET_SYSTEM_CAPTURE_MUTE,
};

enum CRAS_CLIENT_MESSAGE_ID {
	/* Server -> Client */
	CRAS_CLIENT_CONNECTED,
	CRAS_CLIENT_STREAM_CONNECTED,
	CRAS_CLIENT_STREAM_REATTACH,
	CRAS_CLIENT_IODEV_LIST,
	CRAS_CLIENT_VOLUME_UPDATE,
	CRAS_CLIENT_CLIENT_LIST_UPDATE,
};

/* Messages that control the server. These are sent from the client to affect
 * and action on the server. */
struct cras_server_message {
	size_t length;
	enum CRAS_SERVER_MESSAGE_ID id;
};

/* Messages that control the client. These are sent from the server to affect
 * and action on the client. */
struct cras_client_message {
	size_t length;
	enum CRAS_CLIENT_MESSAGE_ID id;
};

/*
 * Messages from client to server.
 */

/* Sent by a client to connect a stream to the server. */
struct cras_connect_message {
	struct cras_server_message header;
	size_t proto_version;
	enum CRAS_STREAM_DIRECTION direction; /* input or output */
	cras_stream_id_t stream_id; /* unique id for this stream */
	enum CRAS_STREAM_TYPE stream_type; /* media, or call, etc. */
	size_t buffer_frames; /* Buffer size in frames. */
	size_t cb_threshold; /* callback client when this much is left */
	size_t min_cb_level; /* don't callback unless this much is avail */
	uint32_t flags;
	struct cras_audio_format format; /* rate, channels, sample size */
};
static inline void cras_fill_connect_message(struct cras_connect_message *m,
					   enum CRAS_STREAM_DIRECTION direction,
					   cras_stream_id_t stream_id,
					   enum CRAS_STREAM_TYPE stream_type,
					   size_t buffer_frames,
					   size_t cb_threshold,
					   size_t min_cb_level,
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
	m->header.id = CRAS_SERVER_CONNECT_STREAM;
	m->header.length = sizeof(struct cras_connect_message);
}

/* Sent by a client to remove a stream from the server. */
struct cras_disconnect_stream_message {
	struct cras_server_message header;
	cras_stream_id_t stream_id;
};
static inline void cras_fill_disconnect_stream_message(
		struct cras_disconnect_stream_message *m,
		cras_stream_id_t stream_id)
{
	m->stream_id = stream_id;
	m->header.id = CRAS_SERVER_DISCONNECT_STREAM;
	m->header.length = sizeof(struct cras_disconnect_stream_message);
}

/* Move streams of "type" to the iodev at "iodev_idx". */
struct cras_switch_stream_type_iodev {
	struct cras_server_message header;
	enum CRAS_STREAM_TYPE stream_type;
	size_t iodev_idx;
};
static inline void fill_cras_switch_stream_type_iodev(
		struct cras_switch_stream_type_iodev *m,
		enum CRAS_STREAM_TYPE stream_type, size_t iodev_idx)
{
	m->stream_type = stream_type;
	m->iodev_idx = iodev_idx;
	m->header.id = CRAS_SERVER_SWITCH_STREAM_TYPE_IODEV;
	m->header.length = sizeof(struct cras_switch_stream_type_iodev);
}

/* Set the system volume. */
struct cras_set_system_volume {
	struct cras_server_message header;
	size_t volume;
};
static inline void cras_fill_set_system_volume(
		struct cras_set_system_volume *m,
		size_t volume)
{
	m->volume = volume;
	m->header.id = CRAS_SERVER_SET_SYSTEM_VOLUME;
	m->header.length = sizeof(*m);
}

/* Sets the capture gain. */
struct cras_set_system_capture_gain {
	struct cras_server_message header;
	long gain;
};
static inline void cras_fill_set_system_capture_gain(
		struct cras_set_system_capture_gain *m,
		long gain)
{
	m->gain = gain;
	m->header.id = CRAS_SERVER_SET_SYSTEM_CAPTURE_GAIN;
	m->header.length = sizeof(*m);
}

/* Set the system mute state. */
struct cras_set_system_mute {
	struct cras_server_message header;
	int mute; /* 0 = un-mute, 1 = mute. */
};
static inline void cras_fill_set_system_mute(
		struct cras_set_system_mute *m,
		int mute)
{
	m->mute = mute;
	m->header.id = CRAS_SERVER_SET_SYSTEM_MUTE;
	m->header.length = sizeof(*m);
}
static inline void cras_fill_set_system_capture_mute(
		struct cras_set_system_mute *m,
		int mute)
{
	m->mute = mute;
	m->header.id = CRAS_SERVER_SET_SYSTEM_CAPTURE_MUTE;
	m->header.length = sizeof(*m);
}

/*
 * Messages sent from server to client.
 */

/* Reply from the server indicating that the client has connected. */
struct cras_client_connected {
	struct cras_client_message header;
	size_t client_id;
};
static inline void cras_fill_client_connected(
		struct cras_client_connected *m,
		size_t client_id)
{
	m->client_id = client_id;
	m->header.id = CRAS_CLIENT_CONNECTED;
	m->header.length = sizeof(struct cras_client_connected);
}

/* Reply from server that a stream has been successfully added. */
struct cras_client_stream_connected {
	struct cras_client_message header;
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
	m->header.id = CRAS_CLIENT_STREAM_CONNECTED;
	m->header.length = sizeof(struct cras_client_stream_connected);
}

/* Reattach a given stream.  This is used to indicate that a stream has been
 * removed from it's device and should be re-attached.  Occurs when moving
 * streams. */
struct cras_client_stream_reattach {
	struct cras_client_message header;
	cras_stream_id_t stream_id;
};
static inline void cras_fill_client_stream_reattach(
		struct cras_client_stream_reattach *m,
		cras_stream_id_t stream_id)
{
	m->stream_id = stream_id;
	m->header.id = CRAS_CLIENT_STREAM_REATTACH;
	m->header.length = sizeof(struct cras_client_stream_reattach);
}

/* Informs clients of the currently list of input and output iodevs. */
struct cras_client_iodev_list {
	struct cras_client_message header;
	size_t num_outputs;
	size_t num_inputs;
	struct cras_iodev_info iodevs[];
};

/* Informs the clients of the system volume and mute states. */
struct cras_client_volume_status {
	struct cras_client_message header;
	size_t volume;
	int muted;
	long capture_gain;
	int capture_muted;
	long volume_min_dBFS;
	long volume_max_dBFS;
	long capture_gain_min_dBFS;
	long capture_gain_max_dBFS;
};
static inline void cras_fill_client_volume_status(
		struct cras_client_volume_status *m,
		size_t volume,
		int muted,
		long capture_gain,
		int capture_muted,
		long volume_min_dBFS,
		long volume_max_dBFS,
		long capture_gain_min_dBFS,
		long capture_gain_max_dBFS)
{
	m->volume = volume;
	m->muted = !!muted;
	m->capture_gain = capture_gain;
	m->capture_muted = capture_muted;
	m->volume_min_dBFS = volume_min_dBFS;
	m->volume_max_dBFS = volume_max_dBFS;
	m->capture_gain_min_dBFS = capture_gain_min_dBFS;
	m->capture_gain_max_dBFS = capture_gain_max_dBFS;
	m->header.id = CRAS_CLIENT_VOLUME_UPDATE;
	m->header.length = sizeof(*m);
}

/* Informs a client of the list of clients attached to the server.  Used only
 * for getting information to log. */
struct cras_client_client_list {
	struct cras_client_message header;
	size_t num_attached_clients;
	struct cras_attached_client_info client_info[];
};

/*
 * Messages specific to passing audio between client and server
 */
enum CRAS_AUDIO_MESSAGE_ID {
	AUDIO_MESSAGE_REQUEST_DATA,
	AUDIO_MESSAGE_DATA_READY,
	NUM_AUDIO_MESSAGES
};

struct audio_message {
	enum CRAS_AUDIO_MESSAGE_ID id;
	int error;
	size_t frames; /* number of samples per channel */
};

#endif /* CRAS_MESSAGES_H_ */
