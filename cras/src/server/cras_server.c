/* Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#define _GNU_SOURCE /* Needed for Linux socket credential passing. */

#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/un.h>
#include <syslog.h>
#include <unistd.h>

#include "cras_config.h"
#include "cras_iodev_list.h"
#include "cras_messages.h"
#include "cras_rclient.h"
#include "cras_server.h"
#include "cras_system_state.h"
#include "cras_util.h"
#include "utlist.h"

/* Store a list of clients that are attached to the server.
 * Members:
 *    id - Unique identifier for this client.
 *    fd - socket file descriptor used to communicate with client.
 *    ucred - Process, user, and group ID of the client.
 *    client - rclient to handle messages from this client.
 */
struct attached_client {
	size_t id;
	int fd;
	struct ucred ucred;
	struct cras_rclient *client;
	struct attached_client *next, *prev;
};

/* Local server data. */
struct server_data {
	struct attached_client *clients_head;
	size_t num_clients;
	size_t next_client_id;
} server_instance;

/* Remove a client from the list and destroy it.  Calling rclient_destroy will
 * also free all the streams owned by the client */
static void remove_client(struct attached_client *client)
{
	close(client->fd);
	DL_DELETE(server_instance.clients_head, client);
	server_instance.num_clients--;
	cras_rclient_destroy(client->client);
	free(client);
}

/* This is called when "select" indicates that the client has written data to
 * the socket.  Read out one message and pass it to the client message handler.
 */
static void handle_message_from_client(struct attached_client *client)
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
	syslog(LOG_ERR, "read err, removing client %zu", client->id);
	remove_client(client);
}

/* Discovers and fills in info about the client that can be obtained from the
 * socket. The pid of the attaching client identifies it in logs. */
static void fill_client_info(struct attached_client *client)
{
	socklen_t ucred_length = sizeof(client->ucred);

	if (getsockopt(client->fd, SOL_SOCKET, SO_PEERCRED,
		       &client->ucred, &ucred_length))
		syslog(LOG_ERR, "Failed to get client socket info\n");
}

/* Sends the current list of clients to all other attached clients. */
static int send_client_list_to_clients(struct server_data *serv)
{
	size_t msg_size;
	struct cras_client_client_list *msg;
	struct attached_client *c;
	struct cras_attached_client_info *info;

	msg_size = sizeof(*msg) +
			sizeof(msg->client_info[0]) * serv->num_clients;
	msg = malloc(msg_size);
	if (msg == NULL)
		return -ENOMEM;

	msg->num_attached_clients = serv->num_clients;
	msg->header.length = msg_size;
	msg->header.id = CRAS_CLIENT_CLIENT_LIST_UPDATE;
	info = msg->client_info;
	DL_FOREACH(serv->clients_head, c) {
		info->id = c->id;
		info->pid = c->ucred.pid;
		info->uid = c->ucred.uid;
		info->gid = c->ucred.gid;
		info++;
	}

	cras_server_send_to_all_clients(&msg->header);
	free(msg);
	return 0;
}

/* Handles requests from a client to attach to the server.  Create a local
 * structure to track the client, assign it a unique id and let it attach */
static void handle_new_connection(struct sockaddr_un *address, int fd)
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
		DL_SEARCH_SCALAR(server_instance.clients_head, out, id,
				 server_instance.next_client_id);
		poll_client->id = server_instance.next_client_id;
		server_instance.next_client_id++;
		if (out == NULL)
			break;
	}

	/* When full, getting an error is preferable to blocking. */
	cras_make_fd_nonblocking(connection_fd);

	poll_client->fd = connection_fd;
	poll_client->next = NULL;
	fill_client_info(poll_client);
	poll_client->client = cras_rclient_create(connection_fd,
						  poll_client->id);
	if (poll_client->client == NULL) {
		syslog(LOG_ERR, "failed to create client");
		close(connection_fd);
		free(poll_client);
		return;
	}

	DL_APPEND(server_instance.clients_head, poll_client);
	server_instance.num_clients++;
	/* Send a current list of available inputs and outputs. */
	cras_iodev_list_update_clients();
	send_client_list_to_clients(&server_instance);
}

/*
 * Exported Interface.
 */

int cras_server_run()
{
	int socket_fd = -1;
	int max_poll_fd;
	fd_set poll_set;
	int rc = 0;
	const char *sockdir;
	struct sockaddr_un addr;
	struct attached_client *elm, *tmp;


	/* Log to syslog. */
	openlog("cras_server", LOG_PID, LOG_USER);

	socket_fd = socket(PF_UNIX, SOCK_STREAM, 0);
	if (socket_fd < 0) {
		syslog(LOG_ERR, "Main server socket failed.");
		rc = socket_fd;
		goto bail;
	}

	sockdir = cras_config_get_system_socket_file_dir();
	if (sockdir == NULL) {
		rc = -ENOTDIR;
		goto bail;
	}

	memset(&addr, 0, sizeof(addr));
	addr.sun_family = AF_UNIX;
	snprintf(addr.sun_path, sizeof(addr.sun_path),
		 "%s/%s", sockdir, CRAS_SOCKET_FILE);
	unlink(addr.sun_path);

	/* Linux quirk: calling fchmod before bind, sets the permissions of the
	 * file created by bind, leaving no window for it to be modified. Start
	 * with very restricted permissions. */
	rc = fchmod(socket_fd, 0700);
	if (rc < 0)
		goto bail;

	if (bind(socket_fd, (struct sockaddr *) &addr,
		 sizeof(struct sockaddr_un)) != 0) {
		syslog(LOG_ERR, "Bind to server socket failed.");
		rc = errno;
		goto bail;
	}

	/* Let other members in our group play audio through this socket. */
	rc = chmod(addr.sun_path, 0770);
	if (rc < 0)
		goto bail;

	if (listen(socket_fd, 5) != 0) {
		syslog(LOG_ERR, "Listen on server socket failed.");
		rc = errno;
		goto bail;
	}

	/* Main server loop - client callbacks are run from this context. */
	while (1) {
		FD_ZERO(&poll_set);
		FD_SET(socket_fd, &poll_set);
		max_poll_fd = socket_fd;
		DL_FOREACH(server_instance.clients_head, elm) {
			if (elm->fd > max_poll_fd)
				max_poll_fd = elm->fd;
			FD_SET(elm->fd, &poll_set);
		}

		rc = select(max_poll_fd + 1, &poll_set, NULL, NULL, NULL);
		if  (rc < 0)
			continue;

		if (FD_ISSET(socket_fd, &poll_set))
			handle_new_connection(&addr, socket_fd);
		DL_FOREACH_SAFE(server_instance.clients_head, elm, tmp)
			if (FD_ISSET(elm->fd, &poll_set))
				handle_message_from_client(elm);
	}

bail:
	if (socket_fd >= 0) {
		close(socket_fd);
		unlink(addr.sun_path);
	}
	return rc;
}

void cras_server_send_to_all_clients(const struct cras_client_message *msg)
{
	struct attached_client *client;

	DL_FOREACH(server_instance.clients_head, client)
		cras_rclient_send_message(client->client, msg);
}
