/* Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <stdint.h>

#include "cras_shm.h"
#include "cras_system_state.h"

#define MAX_VOLUME_TO_SCALE 0.9999999
#define MIN_VOLUME_TO_SCALE 0.0000001

/* Adds src into dst, after scaling by vol.
 * Just hard limits to the min and max S16 value, can be improved later. */
static void scale_add_clip(int16_t *dst,
			   const int16_t *src,
			   size_t count,
			   float vol)
{
	int32_t sum;
	size_t i;

	for (i = 0; i < count; i++) {
		sum = dst[i] + (int16_t)(src[i] * vol);
		if (sum > 0x7fff)
			sum = 0x7fff;
		else if (sum < -0x8000)
			sum = -0x8000;
		dst[i] = sum;
	}
}

/* Adds the first stream to the mix.  Don't need to mix, just setup to the new
 * values. If volume is 1.0, just memcpy. */
static void copy_scaled(int16_t *dst,
			const int16_t *src,
			size_t count,
			float volume_scaler)
{
	int i;

	if (volume_scaler > MAX_VOLUME_TO_SCALE) {
		memcpy(dst, src, count * sizeof(*src));
		return;
	}

	for (i = 0; i < count; i++)
		dst[i] = src[i] * volume_scaler;
}

/* Renders count frames from shm into dst.  Updates count if anything is
 * written. If it's muted and the only stream zero memory. */
size_t cras_mix_add_stream(struct cras_audio_shm *shm,
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
	float mix_vol;

	fr_in_buf = cras_shm_get_frames(shm);
	if (fr_in_buf == 0)
		return 0;
	if (fr_in_buf < *count)
		*count = fr_in_buf;

	/* Stream volume scaler. */
	mix_vol = cras_shm_get_volume_scaler(shm);

	if (cras_shm_get_mute(shm) ||
	    mix_vol < MIN_VOLUME_TO_SCALE) {
		/* Muted, if first then zero fill, otherwise, nop. */
		if (*index == 0)
			memset(dst, 0, *count * num_channels * sizeof(*src));
	} else {
		fr_written = 0;
		while (fr_written < *count) {
			src = cras_shm_get_readable_frames(shm, fr_written,
					&frames);
			if (frames > *count - fr_written)
				frames = *count - fr_written;
			if (frames == 0)
				break;
			num_samples = frames * num_channels;
			if (*index == 0)
				copy_scaled(target, src, num_samples, mix_vol);
			else
				scale_add_clip(target, src,
					       num_samples,
					       mix_vol);
			fr_written += frames;
			target += num_samples;
		}
		*count = fr_written;
	}

	*index = *index + 1;
	return *count;
}

void cras_scale_buffer(int16_t *buffer, unsigned int count, float scaler)
{
	int i;

	if (scaler > MAX_VOLUME_TO_SCALE)
		return;

	for (i = 0; i < count; i++)
		buffer[i] *= scaler;
}
