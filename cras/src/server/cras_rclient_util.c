/* Copyright 2019 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "cras_iodev_list.h"
#include "cras_messages.h"
#include "cras_observer.h"
#include "cras_rclient.h"
#include "cras_rclient_util.h"
#include "cras_rstream.h"
#include "cras_tm.h"
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

void rclient_fill_cras_rstream_config(
	struct cras_rclient *client, const struct cras_connect_message *msg,
	int aud_fd, const struct cras_audio_format *remote_fmt,
	struct cras_rstream_config *stream_config)
{
	stream_config->stream_id = msg->stream_id;
	stream_config->stream_type = msg->stream_type;
	stream_config->client_type = msg->client_type;
	stream_config->direction = msg->direction;
	stream_config->dev_idx = msg->dev_idx;
	stream_config->flags = msg->flags;
	stream_config->effects = msg->effects;
	stream_config->format = remote_fmt;
	stream_config->buffer_frames = msg->buffer_frames;
	stream_config->cb_threshold = msg->cb_threshold;
	stream_config->audio_fd = aud_fd;
	stream_config->client = client;
}
