/* Copyright 2018 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "cras/src/server/server_stream.h"

#include <stdint.h>
#include <stdlib.h>
#include <syslog.h>

#include "cras/src/server/cras_rstream.h"
#include "cras/src/server/cras_rstream_config.h"
#include "cras/src/server/cras_server.h"
#include "cras/src/server/cras_system_state.h"
#include "cras/src/server/stream_list.h"
#include "cras_types.h"
#include "third_party/utlist/utlist.h"

/*
 * Information of a stream created by server. Currently only
 * one server stream is allowed, for each type of server stream.
 */
struct server_stream {
  struct cras_rstream_config config;
  struct stream_list* list;
  cras_stream_id_t stream_id;
};

/*
 * All server streams.
 * Each server stream type is stored in g_server_streams[type].
 */
static struct server_stream* g_server_streams[NUM_SERVER_STREAM_TYPES] = {};

// Actually create the server stream and add to stream list.
static void server_stream_add_cb(void* data) {
  struct cras_rstream* stream = NULL;
  struct server_stream* ss = *(struct server_stream**)data;

  if (ss == NULL) {
    syslog(LOG_WARNING, "Server stream is null before add callback");
    return;
  }

  stream_list_add(ss->list, &ss->config, &stream);
}

int server_stream_create(struct stream_list* stream_list,
                         enum server_stream_type type,
                         unsigned int dev_idx,
                         struct cras_audio_format* format,
                         unsigned int effects,
                         bool synchronous,
                         unsigned int block_size) {
  int audio_fd = -1;
  int client_shm_fd = -1;
  uint64_t buffer_offsets[2] = {0, 0};

  if (g_server_streams[type]) {
    syslog(LOG_ERR, "Server stream of type %d already exists", type);
    return -EEXIST;
  }

  struct server_stream* ss = (struct server_stream*)calloc(1, sizeof(*ss));
  if (!ss) {
    syslog(LOG_ERR, "OOM creating server stream");
    return -ENOMEM;
  }

  enum CRAS_STREAM_DIRECTION direction = type == SERVER_STREAM_SIDETONE_OUTPUT
                                             ? CRAS_STREAM_OUTPUT
                                             : CRAS_STREAM_INPUT;
  uint32_t flags;
  switch (type) {
    case SERVER_STREAM_SIDETONE_OUTPUT:
    case SERVER_STREAM_SIDETONE_INPUT:
      flags = SIDETONE_STREAM;
      break;
    default:
      flags = SERVER_ONLY;
      break;
  }

  cras_rstream_config_init(
      /*client=*/NULL, cras_get_stream_id(SERVER_STREAM_CLIENT_ID, type),
      CRAS_STREAM_TYPE_DEFAULT, CRAS_CLIENT_TYPE_SERVER_STREAM, direction,
      dev_idx, flags, effects, format, block_size, block_size, &audio_fd,
      &client_shm_fd,
      /*client_shm_size=*/0, buffer_offsets, &ss->config);
  ss->list = stream_list;
  ss->stream_id = ss->config.stream_id;

  g_server_streams[type] = ss;
  if (synchronous) {
    struct cras_rstream* stream = NULL;
    int rc = stream_list_add(ss->list, &ss->config, &stream);
    if (rc) {
      g_server_streams[type] = NULL;
      return rc;
    }
  } else {
    // Schedule add stream in the next main thread loop.
    cras_system_add_task(server_stream_add_cb, &g_server_streams[type]);
  }
  return 0;
}

static void server_stream_rm_cb(void* data) {
  struct server_stream* ss = (struct server_stream*)data;

  if (ss == NULL) {
    return;
  }

  /*
   * Input Server stream needs no 'draining' state. Uses stream_list_direct_rm
   * here to prevent recursion.
   */
  if (ss->config.direction == CRAS_STREAM_INPUT &&
      stream_list_direct_rm(ss->list, ss->config.stream_id)) {
    syslog(LOG_WARNING, "Server stream input %x no longer exist",
           ss->config.stream_id);
  } else if (ss->config.direction == CRAS_STREAM_OUTPUT &&
             stream_list_rm(ss->list, ss->config.stream_id)) {
    syslog(LOG_WARNING, "Server stream output %x no longer exist",
           ss->config.stream_id);
  }

  free(ss);
}

void server_stream_destroy(struct stream_list* stream_list,
                           enum server_stream_type type,
                           unsigned int dev_idx) {
  struct server_stream* ss = g_server_streams[type];
  if (!ss || ss->config.dev_idx != dev_idx) {
    syslog(LOG_WARNING, "No server stream to destroy");
    return;
  }

  g_server_streams[type] = NULL;
  server_stream_rm_cb(ss);
}

struct cras_rstream* server_stream_find_by_type(
    struct cras_rstream* all_streams,
    enum server_stream_type type) {
  struct server_stream* ss = g_server_streams[type];
  if (!ss) {
    return NULL;
  }

  struct cras_rstream* rstream;
  DL_FOREACH (all_streams, rstream) {
    if (rstream->stream_id == ss->stream_id) {
      return rstream;
    }
  }
  return NULL;
}
