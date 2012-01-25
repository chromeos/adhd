/* Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/*
 * A remote client to the server.
 */
#ifndef CRAS_RCLIENT_H_
#define CRAS_RCLIENT_H_

struct cras_message;
struct cras_rclient;
struct cras_rstream;

/* Crates an rclient structure.
 * Args:
 *    fd - The file descriptor used for communication with the client.
 *    id - Unique identifier for this client.
 * Returns:
 *    A pointer to the newly created rclient on success, NULL on failure.
 */
struct cras_rclient *cras_rclient_create(int fd, size_t id);

/* Destroys an rclient created with "cras_rclient_create".
 * Args:
 *    client - The client to destroy.
 */
void cras_rclient_destroy(struct cras_rclient *client);

/* Handles a message from the client.
 * Args:
 *    client - The client that received this message.
 *    msg - The message that was sent by the remote client.
 * Returns:
 *    0 on success, otherwise a negative error code.
 */
int cras_rclient_message(struct cras_rclient *client,
			 const struct cras_message *msg);

/* Sends a message to the client.
 * Args:
 *    client - The client to send the message to.
 *    msg - The message to send.
 * Returns:
 *    number of bytes written on success, otherwise a negative error code.
 */
int cras_rclient_send_message(const struct cras_rclient *client,
			      const struct cras_message *msg);

#endif /* CRAS_RCLIENT_H_ */
