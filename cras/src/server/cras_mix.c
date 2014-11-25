/* Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <stdint.h>

#include "cras_system_state.h"

#define MAX_VOLUME_TO_SCALE 0.9999999
#define MIN_VOLUME_TO_SCALE 0.0000001

static void cras_mix_add_clip_s16_le(int16_t *dst,
				     const int16_t *src,
				     size_t count)
{
	int32_t sum;
	size_t i;

	for (i = 0; i < count; i++) {
		sum = dst[i] + src[i];
		if (sum > INT16_MAX)
			sum = INT16_MAX;
		else if (sum < INT16_MIN)
			sum = INT16_MIN;
		dst[i] = sum;
	}
}

/* Adds src into dst, after scaling by vol.
 * Just hard limits to the min and max S16 value, can be improved later. */
static void scale_add_clip_s16_le(int16_t *dst,
				  const int16_t *src,
				  size_t count,
				  float vol)
{
	int32_t sum;
	size_t i;

	if (vol > MAX_VOLUME_TO_SCALE)
		return cras_mix_add_clip_s16_le(dst, src, count);

	for (i = 0; i < count; i++) {
		sum = dst[i] + (int16_t)(src[i] * vol);
		if (sum > INT16_MAX)
			sum = INT16_MAX;
		else if (sum < INT16_MIN)
			sum = INT16_MIN;
		dst[i] = sum;
	}
}

/* Adds the first stream to the mix.  Don't need to mix, just setup to the new
 * values. If volume is 1.0, just memcpy. */
static void copy_scaled_s16_le(int16_t *dst,
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

static void cras_scale_buffer_s16_le(uint8_t *buffer, unsigned int count,
				     float scaler)
{
	int i;
	int16_t *out = (int16_t *)buffer;

	if (scaler > MAX_VOLUME_TO_SCALE)
		return;

	if (scaler < MIN_VOLUME_TO_SCALE) {
		memset(out, 0, count * sizeof(*out));
		return;
	}

	for (i = 0; i < count; i++)
		out[i] *= scaler;
}

static void cras_mix_add_s16_le(uint8_t *dst, uint8_t *src,
				unsigned int count, unsigned int index,
				int mute, float mix_vol)
{
	int16_t *out = (int16_t *)dst;
	int16_t *in = (int16_t *)src;

	if (mute || (mix_vol < MIN_VOLUME_TO_SCALE)) {
		if (index == 0)
			memset(out, 0, count * sizeof(*out));
		return;
	}

	if (index == 0)
		return copy_scaled_s16_le(out, in, count, mix_vol);

	scale_add_clip_s16_le(out, in, count, mix_vol);
}

void cras_scale_buffer(snd_pcm_format_t fmt, uint8_t *buff, unsigned int count,
		       float scaler)
{
	switch (fmt) {
	case SND_PCM_FORMAT_S16_LE:
		return cras_scale_buffer_s16_le(buff, count, scaler);
	default:
		break;
	}
}

void cras_mix_add(snd_pcm_format_t fmt, uint8_t *dst, uint8_t *src,
		  unsigned int count, unsigned int index,
		  int mute, float mix_vol)
{
	switch (fmt) {
	case SND_PCM_FORMAT_S16_LE:
		return cras_mix_add_s16_le(dst, src, count, index, mute,
					   mix_vol);
	default:
		break;
	}
}

size_t cras_mix_mute_buffer(uint8_t *dst,
			    size_t frame_bytes,
			    size_t count)
{
	memset(dst, 0, count * frame_bytes);
	return count;
}
