/* Copyright 2019 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "cras/src/server/cras_playback_rclient.h"

#include <stdio.h>

#include "cras/src/server/cras_iodev_list.h"
#include "cras/src/server/cras_rclient.h"
#include "cras/src/server/cras_rclient_util.h"
#include "cras/src/server/cras_rstream.h"
#include "cras_types.h"

// Declarations of cras_rclient operators for cras_playback_rclient.
static const struct cras_rclient_ops cras_playback_rclient_ops = {
    .handle_message_from_client = rclient_handle_message_from_client,
    .send_message_to_client = rclient_send_message_to_client,
    .destroy = rclient_destroy,
};

/*
 * Exported Functions.
 */

/* Creates a client structure and sends a message back informing the client that
 * the connection has succeeded. */
struct cras_rclient* cras_playback_rclient_create(int fd, size_t id) {
  return rclient_generic_create(fd, id, &cras_playback_rclient_ops,
                                cras_stream_direction_mask(CRAS_STREAM_OUTPUT));
}
