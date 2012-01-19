/* Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/*
 * Types commonly used in the client and server are defined here.
 */
#ifndef CRAS_TYPES_H_
#define CRAS_TYPES_H_

#include <alsa/asoundlib.h>
#include <stdint.h>
#include <stdlib.h>

/* Directions of audio streams. */
enum {
	CRAS_STREAM_OUTPUT,
	CRAS_STREAM_INPUT,
};

/* Types of audio streams. */
enum {
	CRAS_STREAM_TYPE_DEFAULT,
};

/* Audio format. */
struct cras_audio_format {
	snd_pcm_format_t format;
	size_t frame_rate; /* Hz */
	size_t num_channels;
};

/* Create an audio format structure. */
struct cras_audio_format *cras_audio_format_create(snd_pcm_format_t format,
						   size_t frame_rate,
						   size_t num_channels);

/* Destroy an audio format struct created with cras_audio_format_crate. */
void cras_audio_format_destroy(struct cras_audio_format *fmt);

/* Returns the number of bytes per sample.
 * This is bits per smaple / 8 * num_channels.
 */
static inline size_t cras_get_format_bytes(const struct cras_audio_format *fmt)
{
	size_t bytes = snd_pcm_format_physical_width(fmt->format) / 8;
	bytes = bytes * fmt->num_channels;
	return bytes;
}

/* Unique identifier for each active stream.
 * The top 16 bits are the client number, lower 16 are the stream number.
 */
typedef uint32_t cras_stream_id_t;
/* Generates a stream id for client stream. */
static inline cras_stream_id_t cras_get_stream_id(uint16_t client_id,
						  uint16_t stream_id)
{
	return ((client_id & 0x0000ffff) << 16) | (stream_id & 0x0000ffff);
}

#endif /* CRAS_TYPES_H_ */
