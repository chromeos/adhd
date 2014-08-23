/* Copyright (c) 2014 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "dev_stream.h"
#include "cras_audio_area.h"
#include "cras_mix.h"
#include "cras_rstream.h"
#include "cras_shm.h"

struct dev_stream *dev_stream_create(struct cras_rstream *stream)
{
	struct dev_stream *out = calloc(1, sizeof(*out));
	out->stream = stream;

	return out;
}

void dev_stream_destroy(struct dev_stream *dev_stream)
{
	free(dev_stream);
}

unsigned int dev_stream_mix(struct dev_stream *dev_stream,
			    size_t num_channels,
			    uint8_t *dst,
			    size_t *count,
			    size_t *index)
{
	struct cras_audio_shm *shm;
	int16_t *src;
	int16_t *target = (int16_t *)dst;
	unsigned int fr_written;
	int fr_in_buf;
	unsigned int num_samples;
	size_t frames = 0;
	float mix_vol;

	shm = cras_rstream_output_shm(dev_stream->stream);
	fr_in_buf = cras_shm_get_frames(shm);
	if (fr_in_buf <= 0) {
		*count = 0;
		return 0;
	}
	if (fr_in_buf < *count)
		*count = fr_in_buf;

	/* Stream volume scaler. */
	mix_vol = cras_shm_get_volume_scaler(shm);

	fr_written = 0;
	while (fr_written < *count) {
		src = cras_shm_get_readable_frames(shm, fr_written,
				&frames);
		if (frames > *count - fr_written)
			frames = *count - fr_written;
		if (frames == 0)
			break;
		num_samples = frames * num_channels;
		cras_mix_add(target, src, num_samples, *index,
			     cras_shm_get_mute(shm), mix_vol);
		fr_written += frames;
		target += num_samples;
	}

	*count = fr_written;
	cras_shm_buffer_read(shm, *count);

	*index = *index + 1;
	return *count;
}

void dev_stream_capture(const struct dev_stream *dev_stream,
			struct cras_audio_area *area,
			unsigned int offset,
			unsigned int count,
			unsigned int dev_index)
{
	struct cras_rstream *rstream = dev_stream->stream;
	struct cras_audio_shm *shm;
	uint8_t *dst;

	shm = cras_rstream_input_shm(rstream);
	dst = cras_shm_get_writeable_frames(shm,
					    cras_shm_used_frames(shm),
					    NULL);
	cras_audio_area_config_buf_pointers(rstream->input_audio_area,
					    &rstream->format,
					    dst);
	rstream->input_audio_area->frames = cras_shm_used_frames(shm);
	cras_audio_area_copy(rstream->input_audio_area, offset,
			     cras_get_format_bytes(&rstream->format),
			     area, dev_index);
}
