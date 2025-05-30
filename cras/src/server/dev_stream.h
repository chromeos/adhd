/* Copyright 2014 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * The dev_stream structure is used for mapping streams to a device.  In
 * addition to the rstream, other mixing information is stored here.
 */

#ifndef CRAS_SRC_SERVER_DEV_STREAM_H_
#define CRAS_SRC_SERVER_DEV_STREAM_H_

#include <stdint.h>
#include <sys/time.h>

#include "cras/src/server/cras_rstream.h"
#include "cras_types.h"

#ifdef __cplusplus
extern "C" {
#endif

struct cras_audio_area;
struct cras_fmt_conv;
struct cras_iodev;

/*
 * Linked list of streams of audio from/to a client.
 */
struct dev_stream {
  // Index of the hw device.
  unsigned int dev_id;
  // The iodev |stream| is attaching to.
  struct cras_iodev* iodev;
  // The rstream attached to a device.
  struct cras_rstream* stream;
  // Sample rate or format converter.
  struct cras_fmt_conv* conv;
  // The buffer for converter if needed.
  struct byte_buffer* conv_buffer;
  struct cras_audio_area* conv_area;
  // Size of conv_buffer in frames.
  unsigned int conv_buffer_size_frames;
  // Sampling rate of device. This is set when dev_stream is
  // created.
  size_t dev_rate;
  struct dev_stream *prev, *next;
  // For input stream, it should be set to true after it is added
  // into device. For output stream, it should be set to true
  // just before its first fetch to avoid affecting other existing
  // streams.
  int is_running;
};

/*
 * Creates a dev_stream.
 *
 * Args:
 *    stream - The associated rstream.
 *    dev_id - Index of the device.
 *    dev_fmt - The format of the device.
 *    iodev - A pointer to the iodev instance.
 *    cb_ts - A pointer to the initial callback time.
 *    sleep_interval_ts - A pointer to the initial sleep interval.
 *        Set to null to calculate the value from device rate and block size.
 *        Note that we need this argument so that output device sleep interval
 *        can use input device sleep interval in the beginning to have perfect
 *        alignment in WebRTC use case.
 * Returns the pointer to the created dev_stream.
 */
struct dev_stream* dev_stream_create(struct cras_rstream* stream,
                                     unsigned int dev_id,
                                     const struct cras_audio_format* dev_fmt,
                                     struct cras_iodev* iodev,
                                     struct timespec* cb_ts,
                                     const struct timespec* sleep_interval_ts);
void dev_stream_destroy(struct dev_stream* dev_stream);

/*
 * Update the estimated sample rate of the device. For multiple active
 * devices case, the linear resampler will be configured by the estimated
 * rate ration of the main device and the current active device the
 * rstream attaches to.
 *
 * Args:
 *    dev_stream - The structure holding the stream.
 *    dev_rate - The sample rate device is using.
 *    dev_rate_ratio - The ratio of estimated rate and used rate.
 *    main_rate_ratio - The ratio of estimated rate and used rate of
 *        main device.
 *    coarse_rate_adjust - The flag to indicate the direction device
 *        sample rate should adjust to.
 */
void dev_stream_set_dev_rate(struct dev_stream* dev_stream,
                             unsigned int dev_rate,
                             double dev_rate_ratio,
                             double main_rate_ratio,
                             int coarse_rate_adjust);

/*
 * Renders count frames from shm into dst.  Updates count if anything is
 * written. If it's muted and the only stream zero memory.
 * Args:
 *    dev_stream - The struct holding the stream to mix.
 *    format - The format of the audio device.
 *    dst - The destination buffer for mixing.
 *    num_to_write - The number of frames written.
 */
int dev_stream_mix(struct dev_stream* dev_stream,
                   const struct cras_audio_format* fmt,
                   uint8_t* dst,
                   unsigned int num_to_write);

/*
 * Reads from the source into the dev_stream.
 * Args:
 *    dev_stream - The struct holding the stream to mix to.
 *    area - The area to copy audio from.
 *    area_offset - The offset at which to start reading from area.
 *    software_gain_scaler - The software gain scaler.
 */
unsigned int dev_stream_capture(struct dev_stream* dev_stream,
                                const struct cras_audio_area* area,
                                unsigned int area_offset,
                                float software_gain_scaler);

// Returns the number of iodevs this stream has attached to.
int dev_stream_attached_devs(const struct dev_stream* dev_stream);

// Updates the number of queued frames in dev_stream.
void dev_stream_update_frames(const struct dev_stream* dev_stream);

/*
 * Returns the number of playback frames queued in shared memory.  This is a
 * post-format-conversion number.  If the stream is 24k with 10 frames queued
 * and the device is playing at 48k, 20 will be returned.
 */
int dev_stream_playback_frames(const struct dev_stream* dev_stream);

/*
 * Returns the number of frames free to be written to in a capture stream.  This
 * number is also post format conversion, similar to playback_frames above.
 */
unsigned int dev_stream_capture_avail(const struct dev_stream* dev_stream);

/*
 * Returns the callback threshold, if necessary converted from a stream frame
 * count to a device frame count.
 */
unsigned int dev_stream_cb_threshold(const struct dev_stream* dev_stream);

// Update next callback time for the stream.
void dev_stream_update_next_wake_time(struct dev_stream* dev_stream);

/*
 * If enough samples have been captured, post them to the client.
 * TODO(dgreid) - see if this function can be eliminated.
 */
int dev_stream_capture_update_rstream(struct dev_stream* dev_stream);

// Updates the read buffer pointers for the stream.
int dev_stream_playback_update_rstream(struct dev_stream* dev_stream);

/* Fill ts with the time the playback sample will be played.
 * Args:
 *    frame_rate - The sample rate used to calculate the playback time.
 *    frames - The number of frames that before the next written sample is
 *      played.
 *    offset_ms - The duration in milliseconds of additional offset added for
 *      more accurate playback timestamp.
 *    ts - The timestamp the next written sample will be played in DAC.
 */
void cras_set_playback_timestamp(size_t frame_rate,
                                 size_t frames,
                                 int32_t offset_ms,
                                 struct cras_timespec* ts);

/* Fill capture_ts with the time the capture sample was recorded
 * Args:
 *    frame_rate - The sample rate used to calculate the capture time.
 *    frames - The number of frames before the captured sample is read from the
 *      ADC.
 *    offset_ms - Intrinsic device latency will be subtracted from `now_ts`.
 *    now_ts - The current timestamp, used to calculate the capture time.
 *    capture_ts - The timestamp the next capture sample was recorded.
 */
void cras_set_capture_timestamp(size_t frame_rate,
                                size_t frames,
                                int32_t offset_ms,
                                struct timespec* now_ts,
                                struct cras_timespec* capture_ts);

/* Fill shm ts with the time the playback sample will be played or the capture
 * sample was captured depending on the direction of the stream.
 * Args:
 *    delay_frames - The delay reproted by the device, in frames at the device's
 *      sample rate.
 */
int dev_stream_set_delay(const struct dev_stream* dev_stream,
                         unsigned int delay_frames);

// Ask the client for cb_threshold samples of audio to play.
int dev_stream_request_playback_samples(struct dev_stream* dev_stream,
                                        const struct timespec* now);

/*
 * Gets the wake up time for a dev_stream.
 * For an input stream, it considers both needed samples and proper time
 * interval between each callbacks.
 * Args:
 *   dev_stream[in]: The dev_stream to check wake up time.
 *   curr_level[in]: The current level of device.
 *   level_tstamp[in]: The time stamp when getting current level of device.
 *   cap_limit[in]: The number of frames that can be captured across all
 *                  streams.
 *   is_cap_limit_stream[in]: 1 if this stream is causing cap_limit.
 *   wake_time_out[out]: A timespec for wake up time.
 * Returns:
 *   0 on success; negative error code on failure.
 *   A positive value if there is no need to set wake up time for this stream.
 */
int dev_stream_wake_time(struct dev_stream* dev_stream,
                         unsigned int curr_level,
                         struct timespec* level_tstamp,
                         unsigned int cap_limit,
                         int is_cap_limit_stream,
                         struct timespec* wake_time_out);

/*
 * Returns a non-negative fd if the fd is expecting a message and should be
 * added to the list of descriptors to poll.
 */
int dev_stream_poll_stream_fd(const struct dev_stream* dev_stream);

static inline int dev_stream_is_running(struct dev_stream* dev_stream) {
  return dev_stream->is_running;
}

static inline void dev_stream_set_running(struct dev_stream* dev_stream) {
  dev_stream->is_running = 1;
}

static inline const struct timespec* dev_stream_next_cb_ts(
    const struct dev_stream* dev_stream) {
  if (dev_stream->stream->flags & USE_DEV_TIMING) {
    return NULL;
  }

  return &dev_stream->stream->next_cb_ts;
}

static inline const struct timespec* dev_stream_sleep_interval_ts(
    struct dev_stream* dev_stream) {
  return &dev_stream->stream->sleep_interval_ts;
}

int dev_stream_is_pending_reply(const struct dev_stream* dev_stream);

/*
 * Reads any pending audio message from the socket.
 */
int dev_stream_flush_old_audio_messages(struct dev_stream* dev_stream);

#ifdef __cplusplus
}  // extern "C"
#endif

#endif  // CRAS_SRC_SERVER_DEV_STREAM_H_
