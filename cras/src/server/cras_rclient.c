/* Copyright 2012 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "cras/src/server/cras_rclient.h"

#include <stdint.h>
#include <stdlib.h>
#include <syslog.h>

#include "cras/src/server/cras_capture_rclient.h"
#include "cras/src/server/cras_control_rclient.h"
#include "cras/src/server/cras_iodev_list.h"
#include "cras/src/server/cras_playback_rclient.h"
#include "cras/src/server/cras_rstream.h"
#include "cras/src/server/cras_unified_rclient.h"
#include "cras_messages.h"
#include "cras_types.h"

// Removes all streams that the client owns and destroys it.
void cras_rclient_destroy(struct cras_rclient* client) {
  client->ops->destroy(client);
}

/* Entry point for handling a message from the client.  Called from the main
 * server context. */
int cras_rclient_buffer_from_client(struct cras_rclient* client,
                                    const uint8_t* buf,
                                    size_t buf_len,
                                    int* fds,
                                    int num_fds) {
  struct cras_server_message* msg = (struct cras_server_message*)buf;

  if (buf_len < sizeof(*msg)) {
    return -EINVAL;
  }
  if (msg->length != buf_len) {
    return -EINVAL;
  }
  return client->ops->handle_message_from_client(client, msg, fds, num_fds);
}

// Sends a message to the client.
int cras_rclient_send_message(const struct cras_rclient* client,
                              const struct cras_client_message* msg,
                              int* fds,
                              unsigned int num_fds) {
  return client->ops->send_message_to_client(client, msg, fds, num_fds);
}

static void cras_rclient_set_client_type(struct cras_rclient* client,
                                         enum CRAS_CLIENT_TYPE client_type) {
  client->client_type = client_type;
}

struct cras_rclient* cras_rclient_create(int fd,
                                         size_t id,
                                         enum CRAS_CONNECTION_TYPE conn_type) {
  struct cras_rclient* client;
  if (!cras_validate_connection_type(conn_type)) {
    goto error;
  }

  switch (conn_type) {
    case CRAS_CONTROL:
      return cras_control_rclient_create(fd, id);
    case CRAS_PLAYBACK:
      return cras_playback_rclient_create(fd, id);
    case CRAS_CAPTURE:
      return cras_capture_rclient_create(fd, id);
    case CRAS_VMS_LEGACY:
      return cras_playback_rclient_create(fd, id);
    case CRAS_VMS_UNIFIED:
      return cras_unified_rclient_create(fd, id);
    case CRAS_PLUGIN_PLAYBACK:
      client = cras_playback_rclient_create(fd, id);
      cras_rclient_set_client_type(client, CRAS_CLIENT_TYPE_PLUGIN);
      return client;
    case CRAS_PLUGIN_UNIFIED:
      client = cras_unified_rclient_create(fd, id);
      cras_rclient_set_client_type(client, CRAS_CLIENT_TYPE_PLUGIN);
      return client;
    default:
      goto error;
  }

error:
  syslog(LOG_WARNING, "unsupported connection type");
  return NULL;
}
