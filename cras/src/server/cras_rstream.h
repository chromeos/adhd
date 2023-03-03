/* Copyright 2012 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/*
 * Remote Stream - An audio steam from/to a client.
 */
#ifndef CRAS_SRC_SERVER_CRAS_RSTREAM_H_
#define CRAS_SRC_SERVER_CRAS_RSTREAM_H_

#include "cras/src/server/buffer_share.h"
#include "cras/src/server/cras_rstream_config.h"
#include "cras/src/server/cras_stream_apm.h"
#include "cras/src/server/ewma_power.h"
#include "cras_shm.h"
#include "cras_types.h"

struct cras_connect_message;
struct cras_rclient;
struct dev_mix;

// Holds informations about the main active device.
struct main_dev_info {
  // id of the main device.
  int dev_id;
  // pointer to the main device.
  void* dev_ptr;
};

/* cras_rstream is used to manage an active audio stream from
 * a client.  Each client can have any number of open streams for
 * playing or recording.
 */
struct cras_rstream {
  // identifier for this stream.
  cras_stream_id_t stream_id;
  // not used.
  enum CRAS_STREAM_TYPE stream_type;
  // The client type of this stream, like Chrome, ARC++.
  enum CRAS_CLIENT_TYPE client_type;
  // input or output.
  enum CRAS_STREAM_DIRECTION direction;
  // Indicative of what special handling is needed.
  uint32_t flags;
  // Socket for requesting and sending audio buffer events.
  int fd;
  // Buffer size in frames.
  size_t buffer_frames;
  // Callback client when this much is left.
  size_t cb_threshold;
  // The stream is draining and waiting to be removed.
  int is_draining;
  // The info of the main device this stream attaches to.
  struct main_dev_info main_dev;
  // The client who uses this stream.
  struct cras_rclient* client;
  // shared memory
  struct cras_audio_shm* shm;
  // space for playback/capture audio
  struct cras_audio_area* audio_area;
  // format of the stream
  struct cras_audio_format format;
  // Next callback time for this stream.
  struct timespec next_cb_ts;
  // Time between audio callbacks.
  struct timespec sleep_interval_ts;
  // The time of the last stream fetch.
  struct timespec last_fetch_ts;
  // Longest interval between two fetches.
  struct timespec longest_fetch_interval;
  // Number of fetch_interval >
  // acceptable_fetch_interval.
  int num_delayed_fetches;
  // The time when the stream started.
  struct timespec start_ts;
  // The time when the first missed callback happens.
  struct timespec first_missed_cb_ts;
  // State of the buffer from all devices for this stream.
  struct buffer_share* buf_state;
  // Object holding a handful of audio processing module
  // instances.
  struct cras_stream_apm* stream_apm;
  // The ewma instance to calculate stream volume.
  struct ewma_power ewma;
  // Number of iodevs this stream has attached to.
  int num_attached_devs;
  // Number of callback schedules have been missed.
  int num_missed_cb;
  // Cached value of the number of queued frames in shm.
  int queued_frames;
  // True if the stream is a pinned stream, false otherwise.
  int is_pinned;
  // device the stream is pinned, 0 if none.
  uint32_t pinned_dev_idx;
  // True if already notified TRIGGER_ONLY stream, false otherwise.
  int triggered;
  // cb_threshold / sample_rate.
  struct timespec acceptable_fetch_interval;
  struct cras_rstream *prev, *next;
};

/* Creates an rstream.
 * Args:
 *    config - Params for configuration of the new rstream. It's a mutable
 *             borrow.
 *    stream_out - Filled with the newly created stream pointer.
 * Returns:
 *    0 on success, EINVAL if an invalid argument is passed, or ENOMEM if out of
 *    memory.
 */
int cras_rstream_create(struct cras_rstream_config* config,
                        struct cras_rstream** stream_out);

// Destroys an rstream.
void cras_rstream_destroy(struct cras_rstream* stream);

// Gets the id of the stream
static inline cras_stream_id_t cras_rstream_id(
    const struct cras_rstream* stream) {
  return stream->stream_id;
}

// Gets the total buffer size in frames for the given client stream.
static inline size_t cras_rstream_get_buffer_frames(
    const struct cras_rstream* stream) {
  return stream->buffer_frames;
}

// Gets the callback threshold in frames for the given client stream.
static inline size_t cras_rstream_get_cb_threshold(
    const struct cras_rstream* stream) {
  return stream->cb_threshold;
}

// Gets the max write size for the stream.
static inline size_t cras_rstream_get_max_write_frames(
    const struct cras_rstream* stream) {
  if (stream->flags & BULK_AUDIO_OK) {
    return cras_rstream_get_buffer_frames(stream);
  }
  return cras_rstream_get_cb_threshold(stream);
}

// Gets the stream type of this stream.
static inline enum CRAS_STREAM_TYPE cras_rstream_get_type(
    const struct cras_rstream* stream) {
  return stream->stream_type;
}

// Gets the direction (input/output/loopback) of the stream.
static inline enum CRAS_STREAM_DIRECTION cras_rstream_get_direction(
    const struct cras_rstream* stream) {
  return stream->direction;
}

// Gets the format for the stream.
static inline void cras_rstream_set_format(
    struct cras_rstream* stream,
    const struct cras_audio_format* fmt) {
  stream->format = *fmt;
}

// Sets the format for the stream.
static inline int cras_rstream_get_format(const struct cras_rstream* stream,
                                          struct cras_audio_format* fmt) {
  *fmt = stream->format;
  return 0;
}

// Gets the fd to be used to poll this client for audio.
static inline int cras_rstream_get_audio_fd(const struct cras_rstream* stream) {
  return stream->fd;
}

// Gets the is_draning flag.
static inline int cras_rstream_get_is_draining(
    const struct cras_rstream* stream) {
  return stream->is_draining;
}

// Sets the is_draning flag.
static inline void cras_rstream_set_is_draining(struct cras_rstream* stream,
                                                int is_draining) {
  stream->is_draining = is_draining;
}

// Gets the shm fds used for the stream shm
static inline int cras_rstream_get_shm_fds(const struct cras_rstream* stream,
                                           int* header_fd,
                                           int* samples_fd) {
  if (!header_fd || !samples_fd) {
    return -EINVAL;
  }

  *header_fd = stream->shm->header_info.fd;
  *samples_fd = stream->shm->samples_info.fd;

  return 0;
}

// Gets the size of the shm area used for samples for this stream.
static inline size_t cras_rstream_get_samples_shm_size(
    const struct cras_rstream* stream) {
  return cras_shm_samples_size(stream->shm);
}

// Gets shared memory region for this stream.
static inline struct cras_audio_shm* cras_rstream_shm(
    struct cras_rstream* stream) {
  return stream->shm;
}

// Checks if the stream uses an output device.
static inline int stream_uses_output(const struct cras_rstream* s) {
  return cras_stream_uses_output_hw(s->direction);
}

// Checks if the stream uses an input device.
static inline int stream_uses_input(const struct cras_rstream* s) {
  return cras_stream_uses_input_hw(s->direction);
}

static inline int stream_is_server_only(const struct cras_rstream* s) {
  return s->flags & SERVER_ONLY;
}

// Gets the enabled effects of this stream.
unsigned int cras_rstream_get_effects(const struct cras_rstream* stream);

// Gets the format of data after stream specific processing.
struct cras_audio_format* cras_rstream_post_processing_format(
    const struct cras_rstream* stream,
    const struct cras_iodev* idev);

/* Checks how much time has passed since last stream fetch and records
 * the longest fetch interval. */
void cras_rstream_record_fetch_interval(struct cras_rstream* rstream,
                                        const struct timespec* now);

// Requests min_req frames from the client.
int cras_rstream_request_audio(struct cras_rstream* stream,
                               const struct timespec* now);

// Tells a capture client that count frames are ready.
int cras_rstream_audio_ready(struct cras_rstream* stream, size_t count);

// Let the rstream know when a device is added or removed.
void cras_rstream_dev_attach(struct cras_rstream* rstream,
                             unsigned int dev_id,
                             void* dev_ptr);
void cras_rstream_dev_detach(struct cras_rstream* rstream, unsigned int dev_id);

// A device using this stream has read or written samples.
void cras_rstream_dev_offset_update(struct cras_rstream* rstream,
                                    unsigned int frames,
                                    unsigned int dev_id);

void cras_rstream_update_input_write_pointer(struct cras_rstream* rstream);
void cras_rstream_update_output_read_pointer(struct cras_rstream* rstream);

unsigned int cras_rstream_dev_offset(const struct cras_rstream* rstream,
                                     unsigned int dev_id);

static inline unsigned int cras_rstream_level(struct cras_rstream* rstream) {
  const struct cras_audio_shm* shm = cras_rstream_shm(rstream);
  return cras_shm_frames_written(shm);
}

static inline int cras_rstream_input_level_met(struct cras_rstream* rstream) {
  const struct cras_audio_shm* shm = cras_rstream_shm(rstream);
  return cras_shm_frames_written(shm) >= rstream->cb_threshold;
}

/* Updates the number of queued frames in shm. The queued frames should be
 * updated everytime before calling cras_rstream_playable_frames.
 */
void cras_rstream_update_queued_frames(struct cras_rstream* rstream);

// Returns the number of playable samples in shm for the given device id.
unsigned int cras_rstream_playable_frames(struct cras_rstream* rstream,
                                          unsigned int dev_id);

// Returns the volume scaler for this stream.
float cras_rstream_get_volume_scaler(struct cras_rstream* rstream);

/* Returns a pointer to readable frames, fills frames with the number of frames
 * available. */
uint8_t* cras_rstream_get_readable_frames(struct cras_rstream* rstream,
                                          unsigned int offset,
                                          size_t* frames);

// Returns non-zero if the stream is muted.
int cras_rstream_get_mute(const struct cras_rstream* rstream);

/*
 * Returns non-zero if the stream is pending a reply from client.
 * - For playback, stream is waiting for AUDIO_MESSAGE_DATA_READY message from
 *   client.
 * - For capture, stream is waiting for AUDIO_MESSAGE_DATA_CAPTURED message
 *   from client.
 */
int cras_rstream_is_pending_reply(const struct cras_rstream* stream);

/*
 * Reads any pending audio message from the socket.
 */
int cras_rstream_flush_old_audio_messages(struct cras_rstream* stream);

#endif  // CRAS_SRC_SERVER_CRAS_RSTREAM_H_
