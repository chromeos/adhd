/* Copyright (c) 2014 The Chromium OS Author. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "cras_audio_area.h"
#include "cras_util.h"
#include "linear_resampler.h"

/* A linear resampler.
 * Members:
 *    num_channels - The number of channles in once frames.
 *    format_bytes - The size of one frame in bytes.
 *    src_offset - The accumulated offset for resampled src data.
 *    dst_offset - The accumulated offset for resampled dst data.
 *    src_rate - The source sample rate.
 *    dst_rate - The destination sample rate.
 *    factor_num - The numerator of the rate factor used for SRC.
 *    factor_den - The denominator of the rate factor used for SRC.
 *    f - The rate factor used for linear resample.
 */
struct linear_resampler {
	unsigned int num_channels;
	unsigned int format_bytes;
	unsigned int src_offset;
	unsigned int dst_offset;
	unsigned int src_rate;
	unsigned int dst_rate;
	unsigned int factor_num;
	unsigned int factor_den;
	float f;
};

struct linear_resampler *linear_resampler_create(unsigned int num_channels,
					     unsigned int format_bytes,
					     float src_rate,
					     float dst_rate)
{
	struct linear_resampler *lr;

	lr = (struct linear_resampler *)calloc(1, sizeof(*lr));
	lr->num_channels = num_channels;
	lr->format_bytes = format_bytes;

	linear_resampler_set_rates(lr, src_rate, dst_rate);

	return lr;
}

void linear_resampler_destroy(struct linear_resampler *lr)
{
	if (lr)
		free(lr);
}

void linear_resampler_set_rates(struct linear_resampler *lr,
				float from, float to)
{
	lr->src_rate = from;
	lr->dst_rate = to;
	lr->f = (float)to / from;
	lr->factor_num = lr->dst_rate * 100;
	lr->factor_den = lr->src_rate * 100;
	lr->src_offset = 0;
	lr->dst_offset = 0;
}

unsigned int linear_resampler_out_frames_to_in(struct linear_resampler *lr,
					       unsigned int frames)
{
	return cras_frames_at_rate(lr->dst_rate, frames, lr->src_rate);
}

unsigned int linear_resampler_in_frames_to_out(struct linear_resampler *lr,
					       unsigned int frames)
{
	return cras_frames_at_rate(lr->src_rate, frames, lr->dst_rate);
}

int linear_resampler_needed(struct linear_resampler *lr)
{
	return lr->src_rate != lr->dst_rate;
}

unsigned int linear_resampler_resample(struct linear_resampler *lr,
			     uint8_t *src,
			     unsigned int *src_frames,
			     uint8_t *dst,
			     unsigned dst_frames)
{
	int ch;
	unsigned int src_idx = 0;
	unsigned int dst_idx = 0;
	float src_pos;
	int16_t *in, *out;

	for (dst_idx = 0; dst_idx <= dst_frames; dst_idx++) {

		src_pos = (float)(lr->dst_offset + dst_idx) / lr->f;
		if (src_pos > lr->src_offset)
			src_pos -= lr->src_offset;
		else
			src_pos = 0;
		src_idx = (unsigned int)src_pos;

		if (src_pos > *src_frames - 1 || dst_idx >= dst_frames) {
			if (src_pos > *src_frames - 1)
				src_idx = *src_frames - 1;
			break;
		}

		in = (int16_t *)(src + src_idx * lr->format_bytes);
		out = (int16_t *)(dst + dst_idx * lr->format_bytes);

		/* Don't do linear interpolcation if src_pos falls on the
		 * last index. */
		if (src_idx == *src_frames - 1) {
			for (ch = 0; ch < lr->num_channels; ch++)
				out[ch] = in[ch];
		} else {
			for (ch = 0; ch < lr->num_channels; ch++) {
				out[ch] = in[ch] + (src_pos - src_idx) *
					(in[lr->num_channels + ch] - in[ch]);
			}
		}

	}

	*src_frames = src_idx + 1;

	lr->src_offset += *src_frames;
	lr->dst_offset += dst_idx;
	while ((lr->src_offset > lr->factor_den) &&
	       (lr->dst_offset > lr->factor_num)) {
		lr->src_offset -= lr->factor_den;
		lr->dst_offset -= lr->factor_num;
	}

	return dst_idx;
}
