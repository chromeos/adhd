/* Copyright (c) 2014 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * The dev_stream structure is used for mapping streams to a device.  In
 * addition to the rstream, other mixing information is stored here.
 */

#ifndef DEV_STREAM_H_
#define DEV_STREAM_H_

#include <stdint.h>
#include <sys/time.h>

#include "cras_types.h"

struct cras_audio_area;
struct cras_fmt_conv;
struct cras_rstream;

/*
 * Linked list of streams of audio from/to a client.
 * Args:
 *    dev_id - Index of the hw device.
 *    stream - The rstream attached to a device.
 *    conv - Sample rate or format converter.
 *    conv_buffer - The buffer for converter if needed.
 *    conv_buffer_size_frames - Size of conv_buffer in frames.
 *    skip_mix - Don't mix this next time streams are mixed.
 */
struct dev_stream {
	unsigned int dev_id;
	struct cras_rstream *stream;
	struct cras_fmt_conv *conv;
	struct byte_buffer *conv_buffer;
	struct cras_audio_area *conv_area;
	unsigned int conv_buffer_size_frames;
	unsigned int skip_mix;
	struct dev_stream *prev, *next;
};

struct dev_stream *dev_stream_create(struct cras_rstream *stream,
				     unsigned int dev_id,
				     const struct cras_audio_format *dev_fmt);
void dev_stream_destroy(struct dev_stream *dev_stream);

/*
 * Update the sample rate of the device.
 */
void dev_stream_set_dev_rate(struct dev_stream *dev_stream,
			     unsigned int frame_rate);

/*
 * Renders count frames from shm into dst.  Updates count if anything is
 * written. If it's muted and the only stream zero memory.
 * Args:
 *    dev_stream - The struct holding the stream to mix.
 *    num_channels - The number of channels on the device.
 *    dst - The destination buffer for mixing.
 *    num_to_write - The number of frames written.
 *    index - The index of the stream writing to this buffer.
 */
unsigned int dev_stream_mix(struct dev_stream *dev_stream,
			    size_t num_channels,
			    uint8_t *dst,
			    unsigned int num_to_write,
			    size_t *index);

/*
 * Reads froms from the source into the dev_stream.
 * Args:
 *    dev_stream - The struct holding the stream to mix to.
 *    area - The area to copy audio from.
 *    index - The index of the buffer to copy to the dev stream.
 */
void dev_stream_capture(struct dev_stream *dev_stream,
			const struct cras_audio_area *area,
			unsigned int dev_index);

/*
 * Returns the number of playback frames queued in shared memory.  This is a
 * post-format-conversion number.  If the stream is 24k with 10 frames queued
 * and the device is playing at 48k, 20 will be returned.
 */
int dev_stream_playback_frames(const struct dev_stream *dev_stream);

/*
 * Returns the number of frames free to be written to in a capture stream.  This
 * number is also post format conversion, similar to playback_frames above.
 */
unsigned int dev_stream_capture_avail(const struct dev_stream *dev_stream);

/*
 * Returns the callback threshold, if necesary converted from a stream frame
 * count to a device frame count.
 */
unsigned int dev_stream_cb_threshold(const struct dev_stream *dev_stream);

/*
 * If enough samples have been captured, post them to the client.
 * TODO(dgreid) - see if this function can be eliminated.
 */
int dev_stream_capture_update_rstream(struct dev_stream *dev_stream);

/* Updates the read buffer pointers for the stream. */
int dev_stream_playback_update_rstream(struct dev_stream *dev_stream);

/* Fill ts with the time the playback sample will be played. */
void cras_set_playback_timestamp(size_t frame_rate,
				 size_t frames,
				 struct cras_timespec *ts);

/* Fill ts with the time the capture sample was recorded. */
void cras_set_capture_timestamp(size_t frame_rate,
				size_t frames,
				struct cras_timespec *ts);

/* Fill shm ts with the time the playback sample will be played or the capture
 * sample was captured depending on the direction of the stream.
 * Args:
 *    delay_frames - The delay reproted by the device, in frames at the device's
 *      sample rate.
 */
void dev_stream_set_delay(const struct dev_stream *dev_stream,
			  unsigned int delay_frames);

/* Ask the client for cb_threshold samples of audio to play. */
int dev_stream_request_playback_samples(struct dev_stream *dev_stream);

static inline const struct timespec *
dev_stream_next_cb_ts(struct dev_stream *dev_stream)
{
	return &dev_stream->stream->next_cb_ts;
}

static inline const struct timespec *
dev_stream_sleep_interval_ts(struct dev_stream *dev_stream)
{
	return &dev_stream->stream->sleep_interval_ts;
}

#endif /* DEV_STREAM_H_ */
