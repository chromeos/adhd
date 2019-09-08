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
#include "cras_tm.h"
#include "cras_types.h"
#include "cras_util.h"
#include "stream_list.h"

int rclient_send_message_to_client(const struct cras_rclient *client,
				   const struct cras_client_message *msg,
				   int *fds, unsigned int num_fds)
{
	return cras_send_with_fds(client->fd, (const void *)msg, msg->length,
				  fds, num_fds);
}

void rclient_destroy(struct cras_rclient *client)
{
	cras_observer_remove(client->observer);
	stream_list_rm_all_client_streams(cras_iodev_list_get_stream_list(),
					  client);
	free(client);
}

static int
rclient_validate_stream_connect_message(const struct cras_rclient *client,
					const struct cras_connect_message *msg)
{
	if (!cras_valid_stream_id(msg->stream_id, client->id)) {
		syslog(LOG_ERR,
		       "stream_connect: invalid stream_id: %x for "
		       "client: %zx.\n",
		       msg->stream_id, client->id);
		return -EINVAL;
	}

	int direction = cras_stream_direction_mask(msg->direction);
	if (!(client->supported_directions & direction)) {
		syslog(LOG_ERR,
		       "stream_connect: invalid stream direction: %x for "
		       "client: %zx.\n",
		       msg->direction, client->id);
		return -EINVAL;
	}
	return 0;
}

static int rclient_validate_stream_connect_fds(int audio_fd, int client_shm_fd,
					       size_t client_shm_size)
{
	/* check audio_fd is valid. */
	if (audio_fd < 0) {
		syslog(LOG_ERR, "Invalid audio fd in stream connect.\n");
		return -EBADF;
	}

	/* check client_shm_fd is valid if client wants to use client shm. */
	if (client_shm_size > 0 && client_shm_fd < 0) {
		syslog(LOG_ERR,
		       "client_shm_fd must be valid if client_shm_size > 0.\n");
		return -EBADF;
	} else if (client_shm_size == 0 && client_shm_fd >= 0) {
		syslog(LOG_ERR,
		       "client_shm_fd can be valid only if client_shm_size > 0.\n");
		return -EINVAL;
	}
	return 0;
}

int rclient_validate_stream_connect_params(
	const struct cras_rclient *client,
	const struct cras_connect_message *msg, int audio_fd, int client_shm_fd)
{
	int rc;

	rc = rclient_validate_stream_connect_message(client, msg);
	if (rc)
		return rc;

	rc = rclient_validate_stream_connect_fds(audio_fd, client_shm_fd,
						 msg->client_shm_size);
	if (rc)
		return rc;

	return 0;
}
