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
#define CRAS_PROTO_VER 1
#define CRAS_SERV_MAX_MSG_SIZE 256
#define CRAS_CLIENT_MAX_MSG_SIZE 256

/* Message IDs. */
enum CRAS_SERVER_MESSAGE_ID {
	/* Client -> Server*/
	CRAS_SERVER_CONNECT_STREAM,
	CRAS_SERVER_DISCONNECT_STREAM,
	CRAS_SERVER_SWITCH_STREAM_TYPE_IODEV,
	CRAS_SERVER_SET_SYSTEM_VOLUME,
	CRAS_SERVER_SET_SYSTEM_MUTE,
	CRAS_SERVER_SET_USER_MUTE,
	CRAS_SERVER_SET_SYSTEM_MUTE_LOCKED,
	CRAS_SERVER_SET_SYSTEM_CAPTURE_GAIN,
	CRAS_SERVER_SET_SYSTEM_CAPTURE_MUTE,
	CRAS_SERVER_SET_SYSTEM_CAPTURE_MUTE_LOCKED,
	CRAS_SERVER_SET_NODE_ATTR,
	CRAS_SERVER_SELECT_NODE,
	CRAS_SERVER_RELOAD_DSP,
	CRAS_SERVER_DUMP_DSP_INFO,
	CRAS_SERVER_DUMP_AUDIO_THREAD,
};

enum CRAS_CLIENT_MESSAGE_ID {
	/* Server -> Client */
	CRAS_CLIENT_CONNECTED,
	CRAS_CLIENT_STREAM_CONNECTED,
	CRAS_CLIENT_STREAM_REATTACH,
	CRAS_CLIENT_AUDIO_DEBUG_INFO_READY,
};

/* Messages that control the server. These are sent from the client to affect
 * and action on the server. */
struct __attribute__ ((__packed__)) cras_server_message {
	uint32_t length;
	enum CRAS_SERVER_MESSAGE_ID id;
};

/* Messages that control the client. These are sent from the server to affect
 * and action on the client. */
struct __attribute__ ((__packed__)) cras_client_message {
	uint32_t length;
	enum CRAS_CLIENT_MESSAGE_ID id;
};

/*
 * Messages from client to server.
 */

/* Sent by a client to connect a stream to the server. */
struct __attribute__ ((__packed__)) cras_connect_message {
	struct cras_server_message header;
	uint32_t proto_version;
	enum CRAS_STREAM_DIRECTION direction; /* input/output/unified */
	cras_stream_id_t stream_id; /* unique id for this stream */
	enum CRAS_STREAM_TYPE stream_type; /* media, or call, etc. */
	uint32_t buffer_frames; /* Buffer size in frames. */
	uint32_t cb_threshold; /* callback client when this much is left */
	uint32_t min_cb_level; /* don't callback unless this much is avail */
	uint32_t flags;
	struct cras_audio_format_packed format; /* rate, channel, sample size */
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
	pack_cras_audio_format(&m->format, &format);
	m->header.id = CRAS_SERVER_CONNECT_STREAM;
	m->header.length = sizeof(struct cras_connect_message);
}

/* Sent by a client to remove a stream from the server. */
struct __attribute__ ((__packed__)) cras_disconnect_stream_message {
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
struct __attribute__ ((__packed__)) cras_switch_stream_type_iodev {
	struct cras_server_message header;
	enum CRAS_STREAM_TYPE stream_type;
	uint32_t iodev_idx;
};
static inline void fill_cras_switch_stream_type_iodev(
		struct cras_switch_stream_type_iodev *m,
		enum CRAS_STREAM_TYPE stream_type, uint32_t iodev_idx)
{
	m->stream_type = stream_type;
	m->iodev_idx = iodev_idx;
	m->header.id = CRAS_SERVER_SWITCH_STREAM_TYPE_IODEV;
	m->header.length = sizeof(struct cras_switch_stream_type_iodev);
}

/* Set the system volume. */
struct __attribute__ ((__packed__)) cras_set_system_volume {
	struct cras_server_message header;
	uint32_t volume;
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
struct __attribute__ ((__packed__)) cras_set_system_capture_gain {
	struct cras_server_message header;
	int32_t gain;
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
struct __attribute__ ((__packed__)) cras_set_system_mute {
	struct cras_server_message header;
	int32_t mute; /* 0 = un-mute, 1 = mute. */
};
static inline void cras_fill_set_system_mute(
		struct cras_set_system_mute *m,
		int mute)
{
	m->mute = mute;
	m->header.id = CRAS_SERVER_SET_SYSTEM_MUTE;
	m->header.length = sizeof(*m);
}
static inline void cras_fill_set_user_mute(
		struct cras_set_system_mute *m,
		int mute)
{
	m->mute = mute;
	m->header.id = CRAS_SERVER_SET_USER_MUTE;
	m->header.length = sizeof(*m);
}
static inline void cras_fill_set_system_mute_locked(
		struct cras_set_system_mute *m,
		int locked)
{
	m->mute = locked;
	m->header.id = CRAS_SERVER_SET_SYSTEM_MUTE_LOCKED;
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
static inline void cras_fill_set_system_capture_mute_locked(
		struct cras_set_system_mute *m,
		int locked)
{
	m->mute = locked;
	m->header.id = CRAS_SERVER_SET_SYSTEM_CAPTURE_MUTE_LOCKED;
	m->header.length = sizeof(*m);
}

/* Set an attribute of an ionode. */
struct __attribute__ ((__packed__)) cras_set_node_attr {
	struct cras_server_message header;
	cras_node_id_t node_id;
	enum ionode_attr attr;
	int32_t value;
};
static inline void cras_fill_set_node_attr(
		struct cras_set_node_attr *m,
		cras_node_id_t node_id,
		enum ionode_attr attr,
		int value)
{
	m->header.id = CRAS_SERVER_SET_NODE_ATTR;
	m->node_id = node_id;
	m->attr = attr;
	m->value = value;
	m->header.length = sizeof(*m);
}

/* Set an attribute of an ionode. */
struct __attribute__ ((__packed__)) cras_select_node {
	struct cras_server_message header;
	enum CRAS_STREAM_DIRECTION direction;
	cras_node_id_t node_id;
};
static inline void cras_fill_select_node(
		struct cras_select_node *m,
		enum CRAS_STREAM_DIRECTION direction,
		cras_node_id_t node_id)
{
	m->header.id = CRAS_SERVER_SELECT_NODE;
	m->direction = direction;
	m->node_id = node_id;
	m->header.length = sizeof(*m);
}

/* Reload the dsp configuration. */
struct __attribute__ ((__packed__)) cras_reload_dsp {
	struct cras_server_message header;
};
static inline void cras_fill_reload_dsp(
		struct cras_reload_dsp *m)
{
	m->header.id = CRAS_SERVER_RELOAD_DSP;
	m->header.length = sizeof(*m);
}

/* Dump current dsp information to syslog. */
struct __attribute__ ((__packed__)) cras_dump_dsp_info {
	struct cras_server_message header;
};

static inline void cras_fill_dump_dsp_info(
		struct cras_dump_dsp_info *m)
{
	m->header.id = CRAS_SERVER_DUMP_DSP_INFO;
	m->header.length = sizeof(*m);
}

/* Dump current audio thread information to syslog. */
struct __attribute__ ((__packed__)) cras_dump_audio_thread {
	struct cras_server_message header;
};

static inline void cras_fill_dump_audio_thread(
		struct cras_dump_audio_thread *m)
{
	m->header.id = CRAS_SERVER_DUMP_AUDIO_THREAD;
	m->header.length = sizeof(*m);
}

/*
 * Messages sent from server to client.
 */

/* Reply from the server indicating that the client has connected. */
struct __attribute__ ((__packed__)) cras_client_connected {
	struct cras_client_message header;
	uint32_t client_id;
	key_t shm_key;
};
static inline void cras_fill_client_connected(
		struct cras_client_connected *m,
		size_t client_id,
		key_t shm_key)
{
	m->client_id = client_id;
	m->shm_key = shm_key;
	m->header.id = CRAS_CLIENT_CONNECTED;
	m->header.length = sizeof(struct cras_client_connected);
}

/* Reply from server that a stream has been successfully added. */
struct __attribute__ ((__packed__)) cras_client_stream_connected {
	struct cras_client_message header;
	int32_t err;
	cras_stream_id_t stream_id;
	struct cras_audio_format_packed format;
	int32_t input_shm_key;
	int32_t output_shm_key;
	uint32_t shm_max_size;
};
static inline void cras_fill_client_stream_connected(
		struct cras_client_stream_connected *m,
		int err,
		cras_stream_id_t stream_id,
		struct cras_audio_format format,
		int input_shm_key,
		int output_shm_key,
		size_t shm_max_size)
{
	m->err = err;
	m->stream_id = stream_id;
	pack_cras_audio_format(&m->format, &format);
	m->input_shm_key = input_shm_key;
	m->output_shm_key = output_shm_key;
	m->shm_max_size = shm_max_size;
	m->header.id = CRAS_CLIENT_STREAM_CONNECTED;
	m->header.length = sizeof(struct cras_client_stream_connected);
}

/* Reattach a given stream.  This is used to indicate that a stream has been
 * removed from it's device and should be re-attached.  Occurs when moving
 * streams. */
struct __attribute__ ((__packed__)) cras_client_stream_reattach {
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

/* Sent from server to client when audio debug information is requested. */
struct cras_client_audio_debug_info_ready {
	struct cras_client_message header;
};
static inline void cras_fill_client_audio_debug_info_ready(
		struct cras_client_audio_debug_info_ready *m)
{
	m->header.id = CRAS_CLIENT_AUDIO_DEBUG_INFO_READY;
	m->header.length = sizeof(*m);
}

/*
 * Messages specific to passing audio between client and server
 */
enum CRAS_AUDIO_MESSAGE_ID {
	AUDIO_MESSAGE_REQUEST_DATA,
	AUDIO_MESSAGE_DATA_READY,
	AUDIO_MESSAGE_UNIFIED,
	NUM_AUDIO_MESSAGES
};

struct __attribute__ ((__packed__)) audio_message {
	enum CRAS_AUDIO_MESSAGE_ID id;
	int32_t error;
	uint32_t frames; /* number of samples per channel */
};

#endif /* CRAS_MESSAGES_H_ */
