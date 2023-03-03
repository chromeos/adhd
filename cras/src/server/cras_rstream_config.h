/* Copyright 2020 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/*
 * Remote Stream Configuration
 */
#ifndef CRAS_SRC_SERVER_CRAS_RSTREAM_CONFIG_H_
#define CRAS_SRC_SERVER_CRAS_RSTREAM_CONFIG_H_

#include "cras/src/server/buffer_share.h"
#include "cras_shm.h"
#include "cras_types.h"

struct cras_connect_message;
struct dev_mix;

// Config for creating an rstream.
struct cras_rstream_config {
  cras_stream_id_t stream_id;
  // CRAS_STREAM_TYPE.
  enum CRAS_STREAM_TYPE stream_type;
  // CRAS_CLIENT_TYPE.
  enum CRAS_CLIENT_TYPE client_type;
  // CRAS_STREAM_OUTPUT or CRAS_STREAM_INPUT.
  enum CRAS_STREAM_DIRECTION direction;
  // Pin to this device if != NO_DEVICE.
  uint32_t dev_idx;
  // Any special handling for this stream.
  uint32_t flags;
  // Bit map of effects to be enabled on this stream.
  uint32_t effects;
  // The audio format the stream wishes to use.
  const struct cras_audio_format* format;
  // Total number of audio frames to buffer.
  size_t buffer_frames;
  // # of frames when to request more from the client.
  size_t cb_threshold;
  // The fd to read/write audio signals to. May be -1 for server
  // stream. Some functions may mutably borrow the config and move
  // the fd ownership.
  int audio_fd;
  // The shm fd to use to back the samples area. May be -1.
  // Some functions may dup this fd while borrowing the config.
  int client_shm_fd;
  // The size of shm area backed by client_shm_fd.
  size_t client_shm_size;
  // Initial values for buffer_offset for a client shm stream.
  uint32_t buffer_offsets[2];
  // The client that owns this stream.
  struct cras_rclient* client;
};

/* Fills cras_rstream_config with given parameters.
 *
 * Args:
 *   audio_fd - The audio fd pointer from client. Its ownership will be moved to
 *              stream_config.
 *   client_shm_fd - The shared memory fd pointer for samples from client. Its
 *                   ownership will be moved to stream_config.
 *   Other args - See comments in struct cras_rstream_config.
 */
void cras_rstream_config_init(struct cras_rclient* client,
                              cras_stream_id_t stream_id,
                              enum CRAS_STREAM_TYPE stream_type,
                              enum CRAS_CLIENT_TYPE client_type,
                              enum CRAS_STREAM_DIRECTION direction,
                              uint32_t dev_idx,
                              uint32_t flags,
                              uint32_t effects,
                              const struct cras_audio_format* format,
                              size_t buffer_frames,
                              size_t cb_threshold,
                              int* audio_fd,
                              int* client_shm_fd,
                              size_t client_shm_size,
                              const uint64_t buffer_offsets[2],
                              struct cras_rstream_config* stream_config);

/* Fills cras_rstream_config with given parameters and a cras_connect_message.
 *
 * Args:
 *   client - The rclient which handles the connect message.
 *   msg - The cras_connect_message from client.
 *   aud_fd - The audio fd pointer from client. Its ownership will be moved to
 *            stream_config.
 *   client_shm_fd - The shared memory fd pointer for samples from client. Its
 *                   ownership will be moved to stream_config.
 *   remote_format - The remote_format for the config.
 *
 * Returns a cras_rstream_config struct filled in with params from the message.
 */
struct cras_rstream_config cras_rstream_config_init_with_message(
    struct cras_rclient* client,
    const struct cras_connect_message* msg,
    int* aud_fd,
    int* client_shm_fd,
    const struct cras_audio_format* remote_format);

/* Cleans up given cras_rstream_config. All fds inside the config will be
 * closed.
 *
 * Args:
 *   stream_config - The config to be cleaned up.
 */
void cras_rstream_config_cleanup(struct cras_rstream_config* stream_config);

#endif  // CRAS_SRC_SERVER_CRAS_RSTREAM_CONFIG_H_
