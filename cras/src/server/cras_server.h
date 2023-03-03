/* Copyright 2012 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/*
 * The CRAS server instance.
 */
#ifndef CRAS_SRC_SERVER_CRAS_SERVER_H_
#define CRAS_SRC_SERVER_CRAS_SERVER_H_

// Reserver client id 0-15 for internal server usage.
#define RESERVED_CLIENT_IDS 16
#define SERVER_STREAM_CLIENT_ID 1

struct cras_client_message;

/* Initialize some server setup. Mainly to add the select handler first
 * so that client callbacks can be registered before server start running.
 */
int cras_server_init();

/* Runs the CRAS server.  Open the main socket and begin listening for
 * connections and for messages from clients that have connected.
 */
int cras_server_run(unsigned int profile_disable_mask);

// Send a message to all attached clients.
void cras_server_send_to_all_clients(const struct cras_client_message* msg);

#endif  // CRAS_SRC_SERVER_CRAS_SERVER_H_
