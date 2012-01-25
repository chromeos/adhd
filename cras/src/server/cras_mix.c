/* Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <stdint.h>

#include "cras_shm.h"

/* Adds src into dst, if it's the first stream added, just memcpy.
 * Just hard limits to the min and max S16 value, can be improved later. */
void cras_mix_add_buffer(int16_t *dst, const int16_t *src,
			 size_t samples, size_t *index)
{
	int32_t sum;
	size_t i;

	if (*index == 0)
		memcpy(dst, src, samples * sizeof(*dst));
	else
		for (i = 0; i < samples; i++) {
			sum = dst[i] + src[i];
			if (sum > 0x7fff)
				sum = 0x7fff;
			else if (sum < -0x8000)
				sum = -0x8000;
			dst[i] = sum;
		}
}

/* Renders count frames from shm into dst.  Updates count if anything is
 * written. */
size_t cras_mix_add_stream(struct cras_audio_shm_area *shm,
			   size_t num_channels,
			   uint8_t *dst,
			   size_t *count,
			   size_t *index)
{
	int16_t *src;
	int16_t *target = (int16_t *)dst;
	size_t fr_written, fr_in_buf;
	size_t num_samples;
	size_t frames = 0;

	fr_in_buf = cras_shm_get_frames(shm);
	if (fr_in_buf == 0)
		return 0;
	if (fr_in_buf < *count)
		*count = fr_in_buf;

	fr_written = 0;
	while (fr_written < *count) {
		src = cras_shm_get_readable_frames(shm, fr_written,
						   &frames);
		if (frames > *count - fr_written)
			frames = *count - fr_written;
		num_samples = frames * num_channels;
		cras_mix_add_buffer(target, src, num_samples, index);
		fr_written += frames;
		target += num_samples;
	}

	*index = *index + 1;
	return *count;
}
