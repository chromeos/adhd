/* Copyright 2019 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/*
 * Common utility functions for rclients.
 */
#ifndef CRAS_RCLIENT_UTIL_H_
#define CRAS_RCLIENT_UTIL_H_

#define MSG_LEN_VALID(msg, type) ((msg)->length >= sizeof(type))

struct cras_connect_message;
struct cras_rclient;
struct cras_rclient_message;
struct cras_rstream_config;
struct cras_server_message;

/* Sends a message to the client. */
int rclient_send_message_to_client(const struct cras_rclient *client,
				   const struct cras_client_message *msg,
				   int *fds, unsigned int num_fds);

/* Removes all streams that the client owns and destroys it. */
void rclient_destroy(struct cras_rclient *client);

/* Fill cras_rstream_config with given rclient parameters */
void rclient_fill_cras_rstream_config(
	struct cras_rclient *client, const struct cras_connect_message *msg,
	int aud_fd, int client_shm_fd,
	const struct cras_audio_format *remote_format,
	struct cras_rstream_config *stream_config);

/* Checks if the incoming stream connect message contains
 * - stream_id matches client->id.
 * - direction supported by the client.
 *
 * Args:
 *   client - The cras_rclient which gets the message.
 *   msg - cras_connect_message from client.
 *   audio_fd - Audio fd from client.
 *   client_shm_fd - client shared memory fd from client. It can be -1.
 *
 * Returns:
 *   0 on success, negative error on failure.
 */
int rclient_validate_stream_connect_params(
	const struct cras_rclient *client,
	const struct cras_connect_message *msg, int audio_fd,
	int client_shm_fd);

/*
 * Converts an old version of connect message to the correct
 * cras_connect_message. Returns zero on success, negative on failure.
 * Note that this is special check only for libcras transition in
 * clients, from CRAS_PROTO_VER = 3 to 5.
 * TODO(yuhsuan): clean up the function once clients transition is done.
 */
static inline int
convert_connect_message_old(const struct cras_server_message *msg,
			    struct cras_connect_message *cmsg)
{
	struct cras_connect_message_old *old;

	if (!MSG_LEN_VALID(msg, struct cras_connect_message_old))
		return -EINVAL;

	old = (struct cras_connect_message_old *)msg;
	if (old->proto_version != 3 || CRAS_PROTO_VER != 5)
		return -EINVAL;

	memcpy(cmsg, old, sizeof(*old));
	cmsg->client_type = CRAS_CLIENT_TYPE_LEGACY;
	cmsg->client_shm_size = 0;
	return 0;
}

#endif /* CRAS_RCLIENT_UTIL_H_ */
