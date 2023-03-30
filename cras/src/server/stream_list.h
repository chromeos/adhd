/* Copyright 2015 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef CRAS_SRC_SERVER_STREAM_LIST_H_
#define CRAS_SRC_SERVER_STREAM_LIST_H_

#include <stdbool.h>

#include "cras/src/server/cras_tm.h"
#include "cras_types.h"
#include "third_party/utlist/utlist.h"

struct cras_rclient;
struct cras_rstream;
struct cras_rstream_config;
struct cras_audio_format;
struct stream_list;

typedef int(stream_callback)(struct cras_rstream* rstream);
// This function will mutably borrow stream_config.
typedef int(stream_create_func)(struct cras_rstream_config* stream_config,
                                struct cras_rstream** rstream);
typedef void(stream_destroy_func)(struct cras_rstream* rstream);

struct stream_list* stream_list_create(stream_callback* add_cb,
                                       stream_callback* rm_cb,
                                       stream_create_func* create_cb,
                                       stream_destroy_func* destroy_cb,
                                       stream_callback* list_changed_cb,
                                       struct cras_tm* timer_manager);

void stream_list_destroy(struct stream_list* list);

struct cras_rstream* stream_list_get(struct stream_list* list);

/* Creates a cras_rstream from cras_rstream_config and inserts the cras_rstream
 * to stream_list in descending order by channel count.
 *
 * Args:
 *   list - stream_list to add streams.
 *   stream_config - A mutable borrow of cras_rstream_config.
 *   stream - A pointer to place created cras_rstream.
 *
 * Returns:
 *   0 on success. Negative error code on failure.
 */
int stream_list_add(struct stream_list* list,
                    struct cras_rstream_config* stream_config,
                    struct cras_rstream** stream);

int stream_list_rm(struct stream_list* list, cras_stream_id_t id);

/* Removes the stream with the given id directly from stream_list without
 * draining. Only supports streams with direction = CRAS_STREAM_INPUT.
 */
int stream_list_direct_rm(struct stream_list* list, cras_stream_id_t id);

int stream_list_rm_all_client_streams(struct stream_list* list,
                                      struct cras_rclient* rclient);

// Checks if there is a stream pinned to the given device.
bool stream_list_has_pinned_stream(struct stream_list* list,
                                   unsigned int dev_idx);

// Get the number of output streams in the list.
int stream_list_get_num_output(struct stream_list* list);

#endif
