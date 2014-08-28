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

#include "cras_types.h"

struct cras_audio_area;
struct cras_fmt_conv;
struct cras_rstream;

/*
 * Linked list of streams of audio from/to a client.
 * Args:
 *    stream - The rstream attached to a device.
 *    conv - Sample rate or format converter.
 *    conv_buffer - The buffer for converter if needed.
 *    conv_buffer_size_frames - Size of conv_buffer in frames.
 *    skip_mix - Don't mix this next time streams are mixed.
 */
struct dev_stream {
	struct cras_rstream *stream;
	struct cras_fmt_conv *conv;
	uint8_t *conv_buffer;
	unsigned int conv_buffer_size_frames;
	unsigned int skip_mix;
	struct dev_stream *prev, *next;
};

struct dev_stream *dev_stream_create(struct cras_rstream *stream,
				     const struct cras_audio_format *dev_fmt);
void dev_stream_destroy(struct dev_stream *dev_stream);

/*
 * Renders count frames from shm into dst.  Updates count if anything is
 * written. If it's muted and the only stream zero memory.
 * Args:
 *    dev_stream - The struct holding the stream to mix.
 *    num_channels - The number of channels on the device.
 *    dst - The destination buffer for mixing.
 *    count - The number of frames written.
 *    index - The index of the stream writing to this buffer.
 */
unsigned int dev_stream_mix(struct dev_stream *dev_stream,
			    size_t num_channels,
			    uint8_t *dst,
			    size_t *count,
			    size_t *index);

/*
 * Reads froms from the source into the dev_stream.
 * Args:
 *    dev_stream - The struct holding the stream to mix.
 *    area - The area to copy audio from.
 *    offset - How far to seek in dev_stream before starting to copy data.
 *    dst - The destination buffer for mixing.
 *    count - The number of frames to write.
 *    index - The index of the buffer to copy to the dev stream.
 */
void dev_stream_capture(const struct dev_stream *dev_stream,
			struct cras_audio_area *area,
			unsigned int offset,
			unsigned int count,
			unsigned int dev_index);

#endif /* DEV_STREAM_H_ */
