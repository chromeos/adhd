/* Copyright 2018 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef CRAS_SRC_SERVER_SERVER_STREAM_H_
#define CRAS_SRC_SERVER_SERVER_STREAM_H_

#include "cras_audio_format.h"

#ifdef __cplusplus
extern "C" {
#endif

struct stream_list;
struct cras_rstream;

enum server_stream_type {
  SERVER_STREAM_ECHO_REF,
  SERVER_STREAM_VAD,
  SERVER_STREAM_SIDETONE_INPUT,
  SERVER_STREAM_SIDETONE_OUTPUT,
  NUM_SERVER_STREAM_TYPES,
};

/*
 * Creates a server stream pinned to device of given idx.
 * Args:
 *    stream_list - List of stream to add new server stream to.
 *    type - The type of the new server stream. It is only allowed to have a
 *           single instance of each type.
 *    dev_idx - The id of the device that new server stream will pin to.
 *              Or NO_DEVICE to create a non-pinned stream.
 *    format - The audio format for the server stream.
 *    effects - The effects bits for the new server stream.
 *    synchronous - Whether the stream is created immediately or created in the
 *                  next main thread loop.
 * Returns:
 *    0 for success otherwise negative error code.
 */
int server_stream_create(struct stream_list* stream_list,
                         enum server_stream_type type,
                         unsigned int dev_idx,
                         struct cras_audio_format* format,
                         unsigned int effects,
                         bool synchronous);

/*
 * Synchronously destroys existing server stream pinned to device of given idx.
 * Args:
 *    stream_list - List of stream to look up server stream.
 *    type - The type of the server stream to destroy.
 *    dev_idx - The dev_idx that was passed to server_stream_create.
 **/
void server_stream_destroy(struct stream_list* stream_list,
                           enum server_stream_type type,
                           unsigned int dev_idx);

/*
 * Find the cras_rstream of the given type in the stream list.
 */
struct cras_rstream* server_stream_find_by_type(
    struct cras_rstream* all_streams,
    enum server_stream_type type);

#ifdef __cplusplus
}  // extern "C"
#endif

#endif  // CRAS_SRC_SERVER_SERVER_STREAM_H_
