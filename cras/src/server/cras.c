/* Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/select.h>
#include <sys/signal.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/un.h>
#include <syslog.h>
#include <unistd.h>

#include "cras_alsa_card.h"
#include "cras_config.h"
#include "cras_messages.h"
#include "cras_rclient.h"
#include "cras_system_settings.h"
#include "cras_util.h"
#include "utlist.h"

/* Store a list of clients that are attached to the server.  */
struct attached_client {
	size_t id;
	int fd;
	struct cras_rclient *client;
	struct attached_client *next, *prev;
};

/* Local server data. */
struct serv_data {
	struct attached_client *clients_head;
	size_t next_client_id;
};

/* Remove a client from the list and destroy it.  Calling rclient_destroy will
 * also free all the streams owned by the client */
static void remove_client(struct serv_data *serv,
			  struct attached_client *client)
{
	close(client->fd);
	DL_DELETE(serv->clients_head, client);
	cras_rclient_destroy(client->client);
	free(client);
}

/* This is called when "select" indicates that the client has written data to
 * the socket.  Read out one message and pass it to the client message handler.
 */
static void handle_message_from_client(struct serv_data *serv,
				       struct attached_client *client)
{
	uint8_t buf[CRAS_SERV_MAX_MSG_SIZE];
	struct cras_server_message *msg;
	int nread;

	msg = (struct cras_server_message *)buf;
	nread = read(client->fd, buf, sizeof(msg->length));
	if (nread <= 0)
		goto read_error;
	if (msg->length > sizeof(buf))
		goto read_error;
	nread = read(client->fd, buf + nread, msg->length - nread);
	if (nread <= 0)
		goto read_error;
	cras_rclient_message_from_client(client->client, msg);

	return;

read_error:
	syslog(LOG_ERR, "read err, removing client %zu\n", client->id);
	remove_client(serv, client);
}

/* Handles requests from a client to attach to the server.  Create a local
 * structure to track the client, assign it a unique id and let it attach */
static void handle_new_connection(struct serv_data *serv,
				  struct sockaddr_un *address, int fd)
{
	int connection_fd;
	struct attached_client *poll_client;
	socklen_t address_length;

	poll_client = malloc(sizeof(struct attached_client));
	if (poll_client == NULL) {
		syslog(LOG_ERR, "Allocating poll_client");
		return;
	}

	memset(&address_length, 0, sizeof(address_length));
	connection_fd = accept(fd, (struct sockaddr *) address,
			       &address_length);
	if (connection_fd < 0) {
		syslog(LOG_ERR, "connecting");
		free(poll_client);
		return;
	}

	/* find next available client id */
	while (1) {
		struct attached_client *out;
		DL_SEARCH_SCALAR(serv->clients_head, out, id,
				 serv->next_client_id);
		poll_client->id = serv->next_client_id;
		serv->next_client_id++;
		if (out == NULL)
			break;
	}

	poll_client->fd = connection_fd;
	poll_client->next = NULL;
	poll_client->client = cras_rclient_create(connection_fd,
						  poll_client->id);
	if (poll_client->client == NULL) {
		syslog(LOG_ERR, "failed to create client\n");
		close(connection_fd);
		free(poll_client);
		return;
	}

	DL_APPEND(serv->clients_head, poll_client);
}

/* Runs the CRAS server.  Open the main socket and begin listening for
 * connections and for messages from clients that have connected.
 */
static int run_server()
{
	int socket_fd = -1;
	int max_poll_fd;
	fd_set poll_set;
	int rc = 0;
	const char *sockdir;
	struct sockaddr_un addr;
	struct serv_data *serv;
	struct attached_client *elm, *tmp;


	/* Log to syslog. */
	openlog("cras_server", LOG_PID, LOG_USER);

	serv = calloc(1, sizeof(*serv));
	if (serv == NULL)
		return -ENOMEM;

	socket_fd = socket(PF_UNIX, SOCK_STREAM, 0);
	if (socket_fd < 0) {
		perror("socket\n");
		rc = socket_fd;
		goto bail;
	}

	sockdir = cras_config_get_socket_file_dir();
	if (sockdir == NULL) {
		rc = -ENOTDIR;
		goto bail;
	}

	memset(&addr, 0, sizeof(addr));
	addr.sun_family = AF_UNIX;
	snprintf(addr.sun_path, sizeof(addr.sun_path),
		 "%s/%s", sockdir, CRAS_SOCKET_FILE);
	unlink(addr.sun_path);

	if (bind(socket_fd, (struct sockaddr *) &addr,
		 sizeof(struct sockaddr_un)) != 0) {
		perror("bind\n");
		rc = errno;
		goto bail;
	}

	if (listen(socket_fd, 5) != 0) {
		perror("listen\n");
		rc = errno;
		goto bail;
	}

	/* Main server loop - client callbacks are run from this context. */
	while (1) {
		FD_ZERO(&poll_set);
		FD_SET(socket_fd, &poll_set);
		max_poll_fd = socket_fd;
		DL_FOREACH(serv->clients_head, elm) {
			if (elm->fd > max_poll_fd)
				max_poll_fd = elm->fd;
			FD_SET(elm->fd, &poll_set);
		}
		rc = select(max_poll_fd + 1, &poll_set, NULL, NULL, NULL);
		if  (rc > 0) {
			if (FD_ISSET(socket_fd, &poll_set))
				handle_new_connection(serv, &addr, socket_fd);

			DL_FOREACH_SAFE(serv->clients_head, elm, tmp)
				if (FD_ISSET(elm->fd, &poll_set))
					handle_message_from_client(serv, elm);
		}
	}

bail:
	if (socket_fd >= 0) {
		close(socket_fd);
		unlink(addr.sun_path);
	}
	free(serv);
	return rc;
}

// TODO(dgreid) dynamic output adding - remove this hack.
static int setup_devs()
{
	cras_alsa_card_create(0);
	cras_alsa_card_create(1);
	return 0;
}

/* Ignores sigpipe, we'll notice when a read/write fails. */
static void set_signals()
{
	signal(SIGPIPE, SIG_IGN);
}

/* Entry point for the server. */
int main(int argc, char **argv)
{
	set_signals();

	/* Initialize settings. */
	cras_system_settings_init();

	/* Remove this when dynamic device addition works. */
	setup_devs();

	/* Start the server. */
	run_server();

	return 0;
}
