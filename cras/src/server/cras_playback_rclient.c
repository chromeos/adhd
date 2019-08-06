/* Copyright 2019 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <syslog.h>

#include "cras_iodev_list.h"
#include "cras_messages.h"
#include "cras_observer.h"
#include "cras_rclient.h"
#include "cras_rclient_util.h"
#include "cras_rstream.h"
#include "cras_server_metrics.h"
#include "cras_system_state.h"
#include "cras_types.h"
#include "cras_util.h"
#include "stream_list.h"

/* Handles a message from the client to connect a new stream */
static int handle_client_stream_connect(struct cras_rclient *client,
					const struct cras_connect_message *msg,
					int aud_fd, int client_shm_fd)
{
	struct cras_rstream *stream;
	struct cras_client_stream_connected stream_connected;
	struct cras_client_message *reply;
	struct cras_audio_format remote_fmt;
	struct cras_rstream_config stream_config;
	int rc, header_fd, samples_fd;
	int stream_fds[2];

	if (!cras_valid_stream_id(msg->stream_id, client->id)) {
		syslog(LOG_ERR,
		       "stream_connect: invalid stream_id: %x for "
		       "client: %zx.\n",
		       msg->stream_id, client->id);
		rc = -EINVAL;
		goto reply_err;
	}

	if (msg->direction != CRAS_STREAM_OUTPUT) {
		syslog(LOG_ERR, "Invalid stream direction.\n");
		rc = -EINVAL;
		goto reply_err;
	}

	unpack_cras_audio_format(&remote_fmt, &msg->format);

	rc = rclient_validate_stream_connect_fds(aud_fd, client_shm_fd,
						 msg->client_shm_size);
	if (rc)
		goto reply_err;

	/* When full, getting an error is preferable to blocking. */
	cras_make_fd_nonblocking(aud_fd);

	rclient_fill_cras_rstream_config(client, msg, aud_fd, client_shm_fd,
					 &remote_fmt, &stream_config);
	rc = stream_list_add(cras_iodev_list_get_stream_list(), &stream_config,
			     &stream);
	if (rc)
		goto reply_err;

	/* Tell client about the stream setup. */
	syslog(LOG_DEBUG, "Send connected for stream %x\n", msg->stream_id);
	cras_fill_client_stream_connected(
		&stream_connected, 0, /* No error. */
		msg->stream_id, &remote_fmt,
		cras_rstream_get_samples_shm_size(stream),
		cras_rstream_get_effects(stream));
	reply = &stream_connected.header;

	rc = cras_rstream_get_shm_fds(stream, &header_fd, &samples_fd);
	if (rc)
		goto reply_err;

	stream_fds[0] = header_fd;
	/* If we're using client-provided shm, samples_fd here refers to the
	 * same shm area as client_shm_fd */
	stream_fds[1] = samples_fd;

	rc = client->ops->send_message_to_client(client, reply, stream_fds, 2);
	if (rc < 0) {
		syslog(LOG_ERR, "Failed to send connected messaged\n");
		stream_list_rm(cras_iodev_list_get_stream_list(),
			       stream->stream_id);
		goto reply_err;
	}

	/* Metrics logs the stream configurations. */
	cras_server_metrics_stream_config(&stream_config);

	return 0;

reply_err:
	/* Send the error code to the client. */
	cras_fill_client_stream_connected(&stream_connected, rc, msg->stream_id,
					  &remote_fmt, 0, msg->effects);
	reply = &stream_connected.header;
	client->ops->send_message_to_client(client, reply, NULL, 0);

	if (aud_fd >= 0)
		close(aud_fd);
	if (client_shm_fd >= 0)
		close(client_shm_fd);

	return rc;
}

/* Handles messages from the client requesting that a stream be removed from the
 * server. */
static int handle_client_stream_disconnect(
	struct cras_rclient *client,
	const struct cras_disconnect_stream_message *msg)
{
	if (!cras_valid_stream_id(msg->stream_id, client->id)) {
		syslog(LOG_ERR,
		       "stream_disconnect: invalid stream_id: %x for "
		       "client: %zx.\n",
		       msg->stream_id, client->id);
		return -EINVAL;
	}
	return stream_list_rm(cras_iodev_list_get_stream_list(),
			      msg->stream_id);
}

/* Entry point for handling a message from the client.  Called from the main
 * server context. */
static int cpr_handle_message_from_client(struct cras_rclient *client,
					  const struct cras_server_message *msg,
					  int *fds, unsigned int num_fds)
{
	int rc = 0;
	assert(client && msg);

	/* No message needs more than 2 fds. */
	if (num_fds > 2) {
		syslog(LOG_ERR,
		       "Message %d should not have more than 2 fds attached.",
		       msg->id);
		for (int i = 0; i < num_fds; i++)
			if (fds[i] >= 0)
				close(fds[i]);
		return -EINVAL;
	}

	int fd = num_fds > 0 ? fds[0] : -1;

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
			return -EINVAL;
		}
		break;
	}

	switch (msg->id) {
	case CRAS_SERVER_CONNECT_STREAM: {
		int client_shm_fd = num_fds > 1 ? fds[1] : -1;
		struct cras_connect_message cmsg;
		if (MSG_LEN_VALID(msg, struct cras_connect_message)) {
			rc = handle_client_stream_connect(
				client,
				(const struct cras_connect_message *)msg, fd,
				client_shm_fd);
		} else if (!convert_connect_message_old(msg, &cmsg)) {
			rc = handle_client_stream_connect(client, &cmsg, fd,
							  client_shm_fd);
		} else {
			return -EINVAL;
		}
		break;
	}
	case CRAS_SERVER_DISCONNECT_STREAM:
		if (!MSG_LEN_VALID(msg, struct cras_disconnect_stream_message))
			return -EINVAL;
		rc = handle_client_stream_disconnect(
			client,
			(const struct cras_disconnect_stream_message *)msg);
		break;
	default:
		break;
	}

	return rc;
}

/* Declarations of cras_rclient operators for cras_playback_rclient. */
static const struct cras_rclient_ops cras_playback_rclient_ops = {
	.handle_message_from_client = cpr_handle_message_from_client,
	.send_message_to_client = rclient_send_message_to_client,
	.destroy = rclient_destroy,
};

/*
 * Exported Functions.
 */

/* Creates a client structure and sends a message back informing the client that
 * the connection has succeeded. */
struct cras_rclient *cras_playback_rclient_create(int fd, size_t id)
{
	struct cras_rclient *client;
	struct cras_client_connected msg;
	int state_fd;

	client = (struct cras_rclient *)calloc(1, sizeof(struct cras_rclient));
	if (!client)
		return NULL;

	client->fd = fd;
	client->id = id;

	client->ops = &cras_playback_rclient_ops;

	cras_fill_client_connected(&msg, client->id);
	state_fd = cras_sys_state_shm_fd();
	client->ops->send_message_to_client(client, &msg.header, &state_fd, 1);

	return client;
}
