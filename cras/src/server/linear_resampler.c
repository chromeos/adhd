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
 *    to_times_100 - The numerator of the rate factor used for SRC.
 *    from_times_100 - The denominator of the rate factor used for SRC.
 *    f - The rate factor used for linear resample.
 */
struct linear_resampler {
	unsigned int num_channels;
	unsigned int format_bytes;
	unsigned int src_offset;
	unsigned int dst_offset;
	unsigned int to_times_100;
	unsigned int from_times_100;
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
	lr->f = (float)to / from;
	lr->to_times_100 = to * 100;
	lr->from_times_100 = from * 100;
	lr->src_offset = 0;
	lr->dst_offset = 0;
}

unsigned int linear_resampler_out_frames_to_in(struct linear_resampler *lr,
					       unsigned int frames)
{
	return cras_frames_at_rate(lr->to_times_100, frames, lr->from_times_100);
}

unsigned int linear_resampler_in_frames_to_out(struct linear_resampler *lr,
					       unsigned int frames)
{
	return cras_frames_at_rate(lr->from_times_100, frames, lr->to_times_100);
}

int linear_resampler_needed(struct linear_resampler *lr)
{
	return lr->from_times_100 != lr->to_times_100;
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

	/* Check for corner cases so that we can assume both src_idx and
	 * dst_idx are valid with value 0 in the loop below. */
	if (dst_frames == 0 || *src_frames == 0) {
		*src_frames = 0;
		return 0;
	}

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
			/* When this loop stops, dst_idx is always at the last
			 * used index incremented by 1. */
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
	while ((lr->src_offset > lr->from_times_100) &&
	       (lr->dst_offset > lr->to_times_100)) {
		lr->src_offset -= lr->from_times_100;
		lr->dst_offset -= lr->to_times_100;
	}

	return dst_idx;
}
