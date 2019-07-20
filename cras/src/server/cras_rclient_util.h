/* Copyright 2019 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/*
 * Common utility functions for rclients.
 */
#ifndef CRAS_RCLIENT_UTIL_H_
#define CRAS_RCLIENT_UTIL_H_

struct cras_connect_message;
struct cras_rclient;
struct cras_rclient_message;
struct cras_rstream_config;

/* Sends a message to the client. */
int rclient_send_message_to_client(const struct cras_rclient *client,
				   const struct cras_client_message *msg,
				   int *fds, unsigned int num_fds);

/* Removes all streams that the client owns and destroys it. */
void rclient_destroy(struct cras_rclient *client);

/* Fill cras_rstream_config with given rclient parameters */
void rclient_fill_cras_rstream_config(
	struct cras_rclient *client, const struct cras_connect_message *msg,
	int aud_fd, const struct cras_audio_format *remote_format,
	struct cras_rstream_config *stream_config);

#endif /* CRAS_RCLIENT_UTIL_H_ */
