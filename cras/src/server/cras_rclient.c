/* Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <assert.h>
#include <stdlib.h>
#include <syslog.h>

#include "audio_thread.h"
#include "cras_config.h"
#include "cras_dsp.h"
#include "cras_iodev.h"
#include "cras_iodev_list.h"
#include "cras_messages.h"
#include "cras_rclient.h"
#include "cras_rstream.h"
#include "cras_system_state.h"
#include "cras_types.h"
#include "cras_util.h"
#include "utlist.h"

/* An attached client.  This has a list of audio connections and a file
 * descriptor for communication with the client that isn't time critical. */
struct cras_rclient {
	size_t id;
	int fd; /* Connection for client communication. */
	struct cras_rstream *streams;
};

/* Handles a message from the client to connect a new stream */
static int handle_client_stream_connect(struct cras_rclient *client,
					const struct cras_connect_message *msg,
					int aud_fd)
{
	struct cras_rstream *stream = NULL;
	struct cras_iodev *idev, *odev;
	struct cras_client_stream_connected reply;
	struct cras_audio_format fmt, mfmt;
	struct audio_thread *thread;
	int rc;
	size_t buffer_frames, cb_threshold, min_cb_level;

	unpack_cras_audio_format(&mfmt, &msg->format);

	/* check the aud_fd is valid. */
	if (aud_fd < 0) {
		syslog(LOG_ERR, "Invalid fd in stream connect.\n");
		rc = -EINVAL;
		goto reply_err;
	}
	/* When full, getting an error is preferable to blocking. */
	cras_make_fd_nonblocking(aud_fd);

try_again:
	/* Find the iodev for this new connection and connect to it. */
	rc = cras_get_iodev_for_stream_type(msg->stream_type,
					    msg->direction,
					    &idev,
					    &odev);
	if (rc) {
		syslog(LOG_ERR, "No iodev available.\n");
		goto reply_err;
	}

	/* Tell the iodev about the format we want.  fmt will contain the actual
	 * format used after return. */
	fmt = mfmt;
	if (idev)
		cras_iodev_set_format(idev, &fmt);
	else if (odev)
		cras_iodev_set_format(odev, &fmt);

	if (fmt.frame_rate == 0 || msg->format.frame_rate == 0) {
		syslog(LOG_ERR, "frame_rate is zero.");
		rc = -EINVAL;
		goto reply_err;
	}

	/* Scale parameters to the frame rate of the device. */
	buffer_frames = cras_frames_at_rate(msg->format.frame_rate,
					    msg->buffer_frames,
					    fmt.frame_rate);
	if (buffer_frames & 0x01) /* Ensure even buffer size. */
		buffer_frames++;

	cb_threshold = cras_frames_at_rate(msg->format.frame_rate,
					   msg->cb_threshold,
					   fmt.frame_rate);
	min_cb_level = cras_frames_at_rate(msg->format.frame_rate,
					   msg->min_cb_level,
					   fmt.frame_rate);

	/* Create the stream with the modified parameters. */
	rc = cras_rstream_create(msg->stream_id,
				 msg->stream_type,
				 msg->direction,
				 &fmt,
				 buffer_frames,
				 cb_threshold,
				 min_cb_level,
				 msg->flags,
				 client,
				 &stream);
	if (rc < 0) {
		syslog(LOG_ERR, "Failed to create rstream.\n");
		goto reply_err;
	}

	cras_rstream_set_audio_fd(stream, aud_fd);

	/* Now can pass the stream to the thread. */
	thread = cras_iodev_list_get_audio_thread();
	if (thread == NULL) {
		rc = -ENOMEM;
		goto reply_err;
	}

	if (odev && idev)
		cras_iodev_set_format(odev, &fmt);

	DL_APPEND(client->streams, stream);
	rc = audio_thread_add_stream(thread, stream);
	if (rc < 0) {
		syslog(LOG_ERR, "Attach stream failed.\n");
		DL_DELETE(client->streams, stream);
		if (rc == AUDIO_THREAD_OUTPUT_DEV_ERROR) {
			cras_iodev_list_rm_output(odev);
			goto try_again;
		} else if (rc == AUDIO_THREAD_INPUT_DEV_ERROR) {
			cras_iodev_list_rm_input(idev);
			goto try_again;
		} else {
			goto reply_err;
		}
	}

	/* Tell client about the stream setup. */
	syslog(LOG_DEBUG, "Send connected for stream %x\n", msg->stream_id);
	cras_fill_client_stream_connected(
			&reply,
			0, /* No error. */
			msg->stream_id,
			fmt,
			cras_rstream_input_shm_key(stream),
			cras_rstream_output_shm_key(stream),
			cras_rstream_get_total_shm_size(stream));
	rc = cras_rclient_send_message(client, &reply.header);
	if (rc < 0) {
		syslog(LOG_ERR, "Failed to send connected messaged\n");
		audio_thread_rm_stream(thread, stream);
		DL_DELETE(client->streams, stream);
		goto reply_err;
	}

	cras_system_state_stream_added();

	return 0;

reply_err:
	/* Send the error code to the client. */
	cras_fill_client_stream_connected(&reply, rc, msg->stream_id,
					  mfmt, 0, 0, 0);
	cras_rclient_send_message(client, &reply.header);

	if (aud_fd >= 0)
		close(aud_fd);

	if (stream)
		cras_rstream_destroy(stream);

	return rc;
}

/* Removes the stream from the current device it is being played/captured on and
 * from the list of streams for the client. */
static int disconnect_client_stream(struct cras_rclient *client,
				    struct cras_rstream *stream)
{
	struct audio_thread *thread = cras_iodev_list_get_audio_thread();

	if (thread)
		audio_thread_rm_stream(thread, stream);

	close(cras_rstream_get_audio_fd(stream));
	DL_DELETE(client->streams, stream);
	cras_rstream_destroy(stream);

	cras_system_state_stream_removed();

	return 0;
}

/* Handles messages from the client requesting that a stream be removed from the
 * server. */
static int handle_client_stream_disconnect(
		struct cras_rclient *client,
		const struct cras_disconnect_stream_message *msg)
{
	struct cras_rstream *to_disconnect;

	DL_SEARCH_SCALAR(client->streams, to_disconnect, stream_id,
			 msg->stream_id);
	if (!to_disconnect)
		return -EINVAL;

	return disconnect_client_stream(client, to_disconnect);
}

/* Handles a request to move all streams of a type to an iodev at index */
static int handle_switch_stream_type_iodev(
		struct cras_rclient *client,
		const struct cras_switch_stream_type_iodev *msg)
{
	syslog(LOG_DEBUG, "move stream type %d to iodev %u\n",
	       msg->stream_type, msg->iodev_idx);
	return cras_iodev_move_stream_type(msg->stream_type, msg->iodev_idx);
}

/* Handles dumping audio thread debug info back to the client. */
static void dump_audio_thread_info(struct cras_rclient *client)
{
	struct cras_client_audio_debug_info_ready msg;
	struct cras_server_state *state;

	cras_fill_client_audio_debug_info_ready(&msg);
	state = cras_system_state_get_no_lock();
	audio_thread_dump_thread_info(cras_iodev_list_get_audio_thread(),
				      &state->audio_debug_info);
	cras_rclient_send_message(client, &msg.header);
}

/*
 * Exported Functions.
 */

/* Creates a client structure and sends a message back informing the client that
 * the conneciton has succeeded. */
struct cras_rclient *cras_rclient_create(int fd, size_t id)
{
	struct cras_rclient *client;
	struct cras_client_connected msg;

	client = calloc(1, sizeof(struct cras_rclient));
	if (!client)
		return NULL;

	client->fd = fd;
	client->id = id;

	cras_fill_client_connected(&msg, client->id, cras_sys_state_shm_key());
	cras_rclient_send_message(client, &msg.header);

	return client;
}

/* Removes all streams that the client owns and destroys it. */
void cras_rclient_destroy(struct cras_rclient *client)
{
	struct cras_rstream *stream;
	DL_FOREACH(client->streams, stream) {
		disconnect_client_stream(client, stream);
	}
	free(client);
}

/* Entry point for handling a message from the client.  Called from the main
 * server context. */
int cras_rclient_message_from_client(struct cras_rclient *client,
				     const struct cras_server_message *msg,
				     int fd) {
	assert(client && msg);

	/* Most messages should not have a file descriptor. */
	switch (msg->id) {
	case CRAS_SERVER_CONNECT_STREAM:
		break;
	default:
		if (fd != -1) {
			syslog(LOG_ERR,
			       "Message %d should not have fd attached.",
			       msg->id);
			close(fd);
			return -1;
		}
		break;
	}

	switch (msg->id) {
	case CRAS_SERVER_CONNECT_STREAM:
		handle_client_stream_connect(client,
			(const struct cras_connect_message *)msg, fd);
		break;
	case CRAS_SERVER_DISCONNECT_STREAM:
		handle_client_stream_disconnect(client,
			(const struct cras_disconnect_stream_message *)msg);
		break;
	case CRAS_SERVER_SWITCH_STREAM_TYPE_IODEV:
		handle_switch_stream_type_iodev(client,
			(const struct cras_switch_stream_type_iodev *)msg);
		break;
	case CRAS_SERVER_SET_SYSTEM_VOLUME:
		cras_system_set_volume(
			((const struct cras_set_system_volume *)msg)->volume);
		break;
	case CRAS_SERVER_SET_SYSTEM_MUTE:
		cras_system_set_mute(
			((const struct cras_set_system_mute *)msg)->mute);
		break;
	case CRAS_SERVER_SET_USER_MUTE:
		cras_system_set_user_mute(
			((const struct cras_set_system_mute *)msg)->mute);
		break;
	case CRAS_SERVER_SET_SYSTEM_MUTE_LOCKED:
		cras_system_set_mute_locked(
			((const struct cras_set_system_mute *)msg)->mute);
		break;
	case CRAS_SERVER_SET_SYSTEM_CAPTURE_GAIN: {
		const struct cras_set_system_capture_gain *m =
			(const struct cras_set_system_capture_gain *)msg;
		cras_system_set_capture_gain(m->gain);
		break;
	}
	case CRAS_SERVER_SET_SYSTEM_CAPTURE_MUTE:
		cras_system_set_capture_mute(
			((const struct cras_set_system_mute *)msg)->mute);
		break;
	case CRAS_SERVER_SET_SYSTEM_CAPTURE_MUTE_LOCKED:
		cras_system_set_capture_mute_locked(
			((const struct cras_set_system_mute *)msg)->mute);
		break;
	case CRAS_SERVER_SET_NODE_ATTR: {
		const struct cras_set_node_attr *m =
			(const struct cras_set_node_attr *)msg;
		cras_iodev_list_set_node_attr(m->node_id, m->attr, m->value);
		break;
	}
	case CRAS_SERVER_SELECT_NODE: {
		const struct cras_select_node *m =
			(const struct cras_select_node *)msg;
		cras_iodev_list_select_node(m->direction, m->node_id);
		break;
	}
	case CRAS_SERVER_RELOAD_DSP:
		cras_dsp_reload_ini();
		break;
	case CRAS_SERVER_DUMP_DSP_INFO:
		cras_dsp_dump_info();
		break;
	case CRAS_SERVER_DUMP_AUDIO_THREAD:
		dump_audio_thread_info(client);
		break;
	default:
		break;
	}

	return 0;
}

/* Sends a message to the client. */
int cras_rclient_send_message(const struct cras_rclient *client,
			      const struct cras_client_message *msg)
{
	return write(client->fd, msg, msg->length);
}

