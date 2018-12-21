/* Copyright 2019 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
#include <stdint.h>
#include <limits.h>
#include <string.h>

#include "cras_fmt_conv_ops.h"

//TODO: to be removed, fmt_conv_ops don't depend on fmt_conv
#include "cras_fmt_conv.h"

#define MAX(a, b) \
	({ __typeof__ (a) _a = (a); \
	 __typeof__ (b) _b = (b); \
	 _a > _b ? _a : _b; })
#define MIN(a, b) \
	({ __typeof__ (a) _a = (a); \
	 __typeof__ (b) _b = (b); \
	 _a < _b ? _a : _b; })

/*
 * Add and clip.
 */
static int16_t s16_add_and_clip(int16_t a, int16_t b)
{
	int32_t sum;

	a = htole16(a);
	b = htole16(b);
	sum = (int32_t)a + (int32_t)b;
	sum = MAX(sum, SHRT_MIN);
	sum = MIN(sum, SHRT_MAX);
	return (int16_t)le16toh(sum);
}

/*
 * Format converter.
 */
void convert_u8_to_s16le(const uint8_t *in, size_t in_samples, uint8_t *out)
{
	size_t i;
	uint16_t *_out = (uint16_t *)out;

	for (i = 0; i < in_samples; i++, in++, _out++)
		*_out = (uint16_t)((int16_t)*in - 0x80) << 8;
}

void convert_s243le_to_s16le(const uint8_t *in, size_t in_samples, uint8_t *out)
{
	/* find how to calculate in and out size, implement the conversion
	 * between S24_3LE and S16 */

	size_t i;
	int8_t *_in = (int8_t *)in;
	uint16_t *_out = (uint16_t *)out;

	for (i = 0; i < in_samples; i++, _in += 3, _out++)
		memcpy(_out, _in + 1, 2);
}

void convert_s24le_to_s16le(const uint8_t *in, size_t in_samples, uint8_t *out)
{
	size_t i;
	int32_t *_in = (int32_t *)in;
	uint16_t *_out = (uint16_t *)out;

	for (i = 0; i < in_samples; i++, _in++, _out++)
		*_out = (int16_t)((*_in & 0x00ffffff) >> 8);
}

void convert_s32le_to_s16le(const uint8_t *in, size_t in_samples, uint8_t *out)
{
	size_t i;
	int32_t *_in = (int32_t *)in;
	uint16_t *_out = (uint16_t *)out;

	for (i = 0; i < in_samples; i++, _in++, _out++)
		*_out = (int16_t)(*_in >> 16);
}

void convert_s16le_to_u8(const uint8_t *in, size_t in_samples, uint8_t *out)
{
	size_t i;
	int16_t *_in = (int16_t *)in;

	for (i = 0; i < in_samples; i++, _in++, out++)
		*out = (uint8_t)(*_in >> 8) + 128;
}

void convert_s16le_to_s243le(const uint8_t *in, size_t in_samples, uint8_t *out)
{
	size_t i;
	int16_t *_in = (int16_t *)in;
	uint8_t *_out = (uint8_t *)out;

	for (i = 0; i < in_samples; i++, _in++, _out += 3) {
		*_out = 0;
		memcpy(_out + 1, _in, 2);
	}
}

void convert_s16le_to_s24le(const uint8_t *in, size_t in_samples, uint8_t *out)
{
	size_t i;
	int16_t *_in = (int16_t *)in;
	uint32_t *_out = (uint32_t *)out;

	for (i = 0; i < in_samples; i++, _in++, _out++)
		*_out = ((uint32_t)(int32_t)*_in << 8);
}

void convert_s16le_to_s32le(const uint8_t *in, size_t in_samples, uint8_t *out)
{
	size_t i;
	int16_t *_in = (int16_t *)in;
	uint32_t *_out = (uint32_t *)out;

	for (i = 0; i < in_samples; i++, _in++, _out++)
		*_out = ((uint32_t)(int32_t)*_in << 16);
}

/*
 * Channel converter: mono to stereo.
 */
size_t s16_mono_to_stereo(struct cras_fmt_conv *conv,
			  const int16_t *in, size_t in_frames,
			  int16_t *out)
{
	size_t i;

	for (i = 0; i < in_frames; i++) {
		out[2 * i] = in[i];
		out[2 * i + 1] = in[i];
	}
	return in_frames;
}

/*
 * Channel converter: stereo to mono.
 */
size_t s16_stereo_to_mono(struct cras_fmt_conv *conv,
			  const int16_t *in, size_t in_frames,
			  int16_t *out)
{
	size_t i;

	for (i = 0; i < in_frames; i++)
		out[i] = s16_add_and_clip(in[2 * i], in[2 * i + 1]);
	return in_frames;
}

/*
 * Channel converter: mono to 5.1 surround.
 *
 * Fit mono to front center of the output, or split to front left/right
 * if front center is missing from the output channel layout.
 */
size_t s16_mono_to_51(struct cras_fmt_conv *conv,
		      const int16_t *in, size_t in_frames,
		      int16_t *out)
{
	size_t i, left, right, center;

	memset(out, 0, sizeof(*out) * 6 * in_frames);
	left = conv->out_fmt.channel_layout[CRAS_CH_FL];
	right = conv->out_fmt.channel_layout[CRAS_CH_FR];
	center = conv->out_fmt.channel_layout[CRAS_CH_FC];

	if (center != -1)
		for (i = 0; i < in_frames; i++)
			out[6 * i + center] = in[i];
	else if (left != -1 && right != -1)
		for (i = 0; i < in_frames; i++) {
			out[6 * i + right] = in[i] / 2;
			out[6 * i + left] = in[i] / 2;
		}
	else
		/* Select the first channel to convert to as the
		 * default behavior.
		 */
		for (i = 0; i < in_frames; i++)
			out[6 * i] = in[i];

	return in_frames;
}

/*
 * Channel converter: stereo to 5.1 surround.
 *
 * Fit the left/right of input to the front left/right of output respectively
 * and fill others with zero. If any of the front left/right is missed from
 * the output channel layout, mix to front center.
 */
size_t s16_stereo_to_51(struct cras_fmt_conv *conv,
			const int16_t *in, size_t in_frames,
			int16_t *out)
{
	size_t i, left, right, center;

	memset(out, 0, sizeof(*out) * 6 * in_frames);
	left = conv->out_fmt.channel_layout[CRAS_CH_FL];
	right = conv->out_fmt.channel_layout[CRAS_CH_FR];
	center = conv->out_fmt.channel_layout[CRAS_CH_FC];

	if (left != -1 && right != -1)
		for (i = 0; i < in_frames; i++) {
			out[6 * i + left] = in[2 * i];
			out[6 * i + right] = in[2 * i + 1];
		}
	else if (center != -1)
		for (i = 0; i < in_frames; i++)
			out[6 * i + center] = s16_add_and_clip(
					in[2 * i], in[2 * i + 1]);
	else
		/* Select the first two channels to convert to as the
		 * default behavior.
		 */
		for (i = 0; i < in_frames; i++) {
			out[6 * i] = in[2 * i];
			out[6 * i + 1] = in[2 * i + 1];
		}

	return in_frames;
}

/*
 * Channel converter: 5.1 surround to stereo.
 *
 * The out buffer can have room for just stereo samples. This convert function
 * is used as the default behavior when channel layout is not set from the
 * client side.
 */
size_t s16_51_to_stereo(struct cras_fmt_conv *conv,
			const int16_t *in, size_t in_frames,
			int16_t *out)
{
	static const unsigned int left_idx = 0;
	static const unsigned int right_idx = 1;
	/* static const unsigned int left_surround_idx = 2; */
	/* static const unsigned int right_surround_idx = 3; */
	static const unsigned int center_idx = 4;
	/* static const unsigned int lfe_idx = 5; */
	size_t i;

	for (i = 0; i < in_frames; i++) {
		unsigned int half_center;

		half_center = in[6 * i + center_idx] / 2;
		out[2 * i + left_idx] = s16_add_and_clip(in[6 * i + left_idx],
							 half_center);
		out[2 * i + right_idx] = s16_add_and_clip(in[6 * i + right_idx],
							  half_center);
	}
	return in_frames;
}

/*
 * Channel converter: stereo to quad (front L/R, rear L/R).
 *
 * Fit left/right of input to the front left/right of output respectively
 * and fill others with zero.
 */
size_t s16_stereo_to_quad(struct cras_fmt_conv *conv,
			  const int16_t *in, size_t in_frames,
			  int16_t *out)
{
	size_t i, front_left, front_right, rear_left, rear_right;

	front_left = conv->out_fmt.channel_layout[CRAS_CH_FL];
	front_right = conv->out_fmt.channel_layout[CRAS_CH_FR];
	rear_left = conv->out_fmt.channel_layout[CRAS_CH_RL];
	rear_right = conv->out_fmt.channel_layout[CRAS_CH_RR];

	if (front_left != -1 && front_right != -1 &&
	    rear_left != -1 && rear_right != -1)
		for (i = 0; i < in_frames; i++) {
			out[4 * i + front_left] = in[2 * i];
			out[4 * i + front_right] = in[2 * i + 1];
			out[4 * i + rear_left] = in[2 * i];
			out[4 * i + rear_right] = in[2 * i + 1];
		}
	else
		/* Select the first four channels to convert to as the
		 * default behavior.
		 */
		for (i = 0; i < in_frames; i++) {
			out[4 * i] = in[2 * i];
			out[4 * i + 1] = in[2 * i + 1];
			out[4 * i + 2] = in[2 * i];
			out[4 * i + 3] = in[2 * i + 1];
		}

	return in_frames;
}

/*
 * Channel converter: quad (front L/R, rear L/R) to stereo.
 */
size_t s16_quad_to_stereo(struct cras_fmt_conv *conv,
			  const int16_t *in, size_t in_frames,
			  int16_t *out)
{
	size_t i;
	unsigned int left_idx =
			conv->in_fmt.channel_layout[CRAS_CH_FL];
	unsigned int right_idx =
			conv->in_fmt.channel_layout[CRAS_CH_FR];
	unsigned int left_rear_idx =
			conv->in_fmt.channel_layout[CRAS_CH_RL];
	unsigned int right_rear_idx =
			conv->in_fmt.channel_layout[CRAS_CH_RR];

	if (left_idx == -1 || right_idx == -1 ||
	    left_rear_idx == -1 || right_rear_idx == -1) {
		left_idx = 0;
		right_idx = 1;
		left_rear_idx = 2;
		right_rear_idx = 3;
	}

	for (i = 0; i < in_frames; i++) {
		out[2 * i] = s16_add_and_clip(
		    in[4 * i + left_idx],
		    in[4 * i + left_rear_idx] / 4);
		out[2 * i + 1] = s16_add_and_clip(
		    in[4 * i + right_idx],
		    in[4 * i + right_rear_idx] / 4);
	}
	return in_frames;
}

/*
 * Channel converter: N channels to M channels.
 *
 * The out buffer must have room for M channel. This convert function is used
 * as the default behavior when channel layout is not set from the client side.
 */
size_t s16_default_all_to_all(struct cras_fmt_conv *conv,
			      const int16_t *in, size_t in_frames,
			      int16_t *out)
{
	unsigned int num_in_ch = conv->in_fmt.num_channels;
	unsigned int num_out_ch = conv->out_fmt.num_channels;
	unsigned int in_ch, out_ch, i;

	memset(out, 0, in_frames * cras_get_format_bytes(&conv->out_fmt));
	for (out_ch = 0; out_ch < num_out_ch; out_ch++) {
		for (in_ch = 0; in_ch < num_in_ch; in_ch++) {
			for (i = 0; i < in_frames; i++) {
				out[out_ch + i * num_out_ch] +=
					in[in_ch + i * num_in_ch] / num_in_ch;
			}
		}
	}
	return in_frames;
}

/*
 * Multiplies buffer vector with coefficient vector.
 */
int16_t multiply_buf_with_coef(float *coef, const int16_t *buf, size_t size)
{
	int32_t sum = 0;
	int i;

	for (i = 0; i < size; i++)
		sum += coef[i] * buf[i];
	sum = MAX(sum, -0x8000);
	sum = MIN(sum, 0x7fff);
	return (int16_t)sum;
}

/*
 * Channel layout converter.
 *
 * Converts channels based on the channel conversion coefficient matrix.
 */
size_t convert_channels(struct cras_fmt_conv *conv,
			const int16_t *in, size_t in_frames,
			int16_t *out)
{
	unsigned i, fr;
	unsigned in_idx = 0;
	unsigned out_idx = 0;

	for (fr = 0; fr < in_frames; fr++) {
		for (i = 0; i < conv->out_fmt.num_channels; i++)
			out[out_idx + i] = multiply_buf_with_coef(
					conv->ch_conv_mtx[i],
					&in[in_idx],
					conv->in_fmt.num_channels);
		in_idx += conv->in_fmt.num_channels;
		out_idx += conv->out_fmt.num_channels;
	}

	return in_frames;
}
