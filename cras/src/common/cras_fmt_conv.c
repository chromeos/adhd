/* Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* For now just use speex, can add more resamplers later. */
#include <speex/speex_resampler.h>
#include <syslog.h>

#include "cras_fmt_conv.h"
#include "cras_types.h"
#include "cras_util.h"

/* Want the fastest conversion we can get. */
#define SPEEX_QUALITY_LEVEL 0
/* Max number of converters, src, down/up mix, and format. */
#define MAX_NUM_CONVERTERS 4

typedef void (*sample_format_converter_t)(const uint8_t *in,
					  size_t in_samples,
					  int16_t *out);
typedef size_t (*channel_converter_t)(const int16_t *in,
				      size_t in_frames,
				      int16_t *out);

/* Member data for the resampler. */
struct cras_fmt_conv {
	SpeexResamplerState *speex_state;
	channel_converter_t channel_converter;
	sample_format_converter_t sample_format_converter;
	struct cras_audio_format in_fmt;
	struct cras_audio_format out_fmt;
	uint8_t *input_buf; /* Input buffer, samples to start conversion. */
	uint8_t *tmp_buf; /* Buffer for holding samples between converters. */
	size_t num_converters; /* Incremented once for SRC, channel, format. */
};

/* Add and clip two s16 samples. */
static int16_t s16_add_and_clip(int16_t a, int16_t b)
{
	int32_t sum;

	sum = a + b;
	sum = max(sum, -0x8000);
	sum = min(sum, 0x7fff);
	return (int16_t)sum;
}

/*
 * Convert between different sample formats.
 */

/* Converts from U8 to S16. */
static void convert_u8_to_s16le(const uint8_t *in, size_t in_samples,
				int16_t *out)
{
	size_t i;

	for (i = 0; i < in_samples; i++, in++, out++)
		*out = ((int16_t)*in - 0x80) << 8;
}

/* Converts from S24 to S16. */
static void convert_s24le_to_s16le(const uint8_t *in, size_t in_samples,
				   int16_t *out)
{
	size_t i;
	int32_t *_in = (int32_t *)in;

	for (i = 0; i < in_samples; i++, _in++, out++)
		*out = (int16_t)((*_in & 0x00ffffff) >> 8);
}

/* Converts from S32 to S16. */
static void convert_s32le_to_s16le(const uint8_t *in, size_t in_samples,
				   int16_t *out)
{
	size_t i;
	int32_t *_in = (int32_t *)in;

	for (i = 0; i < in_samples; i++, _in++, out++)
		*out = (int16_t)(*_in >> 16);
}

/*
 * Convert between different channel numbers.
 */

/* Converts S16 mono to S16 stereo. The out buffer must be double the size of
 * the input buffer. */
static size_t s16_mono_to_stereo(const int16_t *in, size_t in_frames,
				 int16_t *out)
{
	size_t i;

	for (i = 0; i < in_frames; i++) {
		out[2 * i] = in[i];
		out[2 * i + 1] = in[i];
	}
	return in_frames;
}

/* Converts S16 Stereo to S16 mono.  The output buffer only need be big enough
 * for mono samples. */
static size_t s16_stereo_to_mono(const int16_t *in, size_t in_frames,
				 int16_t *out)
{
	size_t i;

	for (i = 0; i < in_frames; i++)
		out[i] = s16_add_and_clip(in[2 * i], in[2 * i + 1]);
	return in_frames;
}

/* Converts S16 5.1 to S16 stereo. The out buffer can have room for just
 * stereo samples. */
static size_t s16_51_to_stereo(const int16_t *in, size_t in_frames,
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

		/* TODO(dgreid) - Dont' drop surrounds and LFE! */
		half_center = in[6 * i + center_idx] / 2;
		out[2 * i + left_idx] = s16_add_and_clip(in[6 * i + left_idx],
							 half_center);
		out[2 * i + right_idx] = s16_add_and_clip(in[6 * i + right_idx],
							 half_center);
	}
	return in_frames;
}

/*
 * External interface
 */

struct cras_fmt_conv *cras_fmt_conv_create(const struct cras_audio_format *in,
					   const struct cras_audio_format *out,
					   size_t max_frames)
{
	struct cras_fmt_conv *conv;
	int rc;

	/* Only support S16LE output samples. */
	if (out->format != SND_PCM_FORMAT_S16_LE) {
		syslog(LOG_ERR, "Invalid output format %d", in->format);
		return NULL;
	}

	conv = calloc(1, sizeof(*conv));
	if (conv == NULL)
		return NULL;
	conv->in_fmt = *in;
	conv->out_fmt = *out;

	conv->input_buf = malloc(max_frames * cras_get_format_bytes(in));
	if (conv->input_buf == NULL) {
		free(conv);
		return NULL;
	}

	/* Set up sample format conversion. */
	if (out->format != in->format) {
		conv->num_converters++;
		syslog(LOG_DEBUG, "Convert from format %d to %d.",
		       in->format, out->format);
		switch(in->format) {
		case SND_PCM_FORMAT_U8:
			conv->sample_format_converter = convert_u8_to_s16le;
			break;
		case SND_PCM_FORMAT_S24_LE:
			conv->sample_format_converter = convert_s24le_to_s16le;
			break;
		case SND_PCM_FORMAT_S32_LE:
			conv->sample_format_converter = convert_s32le_to_s16le;
			break;
		default:
			syslog(LOG_ERR, "Invalid sample format %d", in->format);
			cras_fmt_conv_destroy(conv);
			return NULL;
		}
	}
	/* Set up channel number conversion. */
	if (in->num_channels != out->num_channels) {
		conv->num_converters++;
		syslog(LOG_DEBUG, "Convert from %zu to %zu channels.",
		       in->num_channels, out->num_channels);
		if (in->num_channels == 1 && out->num_channels == 2) {
			conv->channel_converter = s16_mono_to_stereo;
		} else if (in->num_channels == 2 && out->num_channels == 1) {
			conv->channel_converter = s16_stereo_to_mono;
		} else if (in->num_channels == 6 && out->num_channels == 2) {
			conv->channel_converter = s16_51_to_stereo;
		} else {
			syslog(LOG_ERR, "Invalid channel conversion %zu to %zu",
			       in->num_channels, out->num_channels);
			cras_fmt_conv_destroy(conv);
			return NULL;
		}
	}
	/* Set up sample rate conversion. */
	if (in->frame_rate != out->frame_rate) {
		conv->num_converters++;
		syslog(LOG_DEBUG, "Convert from %zu to %zu Hz.",
		       in->frame_rate, out->frame_rate);
		conv->speex_state = speex_resampler_init(out->num_channels,
							 in->frame_rate,
							 out->frame_rate,
							 SPEEX_QUALITY_LEVEL,
							 &rc);
		if (conv->speex_state == NULL) {
			syslog(LOG_ERR, "Fail to create speex:%zu %zu %zu %d",
			       out->num_channels,
			       in->frame_rate,
			       out->frame_rate,
			       rc);
			cras_fmt_conv_destroy(conv);
			return NULL;
		}
	}

	if (conv->num_converters > 1) {
		/* Need a temporary area before channel conversion. */
		conv->tmp_buf = malloc(
			max_frames *
			4 * /* width in bytes largest format. */
			max(in->num_channels, out->num_channels));
		if (conv->tmp_buf == NULL) {
			cras_fmt_conv_destroy(conv);
			return NULL;
		}
	}

	assert(conv->num_converters < MAX_NUM_CONVERTERS);

	return conv;
}

void cras_fmt_conv_destroy(struct cras_fmt_conv *conv)
{
	if (conv->speex_state)
		speex_resampler_destroy(conv->speex_state);
	free(conv->tmp_buf);
	free(conv->input_buf);
	free(conv);
}

uint8_t *cras_fmt_conv_get_buffer(struct cras_fmt_conv *conv)
{
	return conv->input_buf;
}

size_t cras_fmt_conv_in_frames_to_out(struct cras_fmt_conv *conv,
				      size_t in_frames)
{
	return cras_frames_at_rate(conv->in_fmt.frame_rate,
				   in_frames,
				   conv->out_fmt.frame_rate);
}

size_t cras_fmt_conv_out_frames_to_in(struct cras_fmt_conv *conv,
				      size_t out_frames)
{
	return cras_frames_at_rate(conv->out_fmt.frame_rate,
				   out_frames,
				   conv->in_fmt.frame_rate);
}

size_t cras_fmt_conv_convert_to(struct cras_fmt_conv *conv, uint8_t *out_buf,
				size_t in_frames)
{
	uint32_t fr_in, fr_out;
	uint8_t *buffers[MAX_NUM_CONVERTERS];
	size_t buf_idx = 0;

	assert(conv);

	fr_in = in_frames;
	fr_out = fr_in;

	/* Set up a chain of buffers.  The output buffer of the first conversion
	 * is used as input to the second and so forth, ending in the output
	 * buffer. */
	buffers[0] = (uint8_t *)conv->input_buf;
	if (conv->num_converters == 2) {
		buffers[1] = (uint8_t *)conv->tmp_buf;
	} else if (conv->num_converters == 3) {
		buffers[1] = (uint8_t *)out_buf;
		buffers[2] = (uint8_t *)conv->tmp_buf;
	}
	buffers[conv->num_converters] = out_buf;

	/* Start with format conversion. */
	if (conv->sample_format_converter != NULL) {
		conv->sample_format_converter(buffers[buf_idx],
					      fr_in * conv->in_fmt.num_channels,
					      (int16_t *)buffers[buf_idx + 1]);
		buf_idx++;
	}

	/* Then channel conversion. */
	if (conv->channel_converter != NULL) {
		conv->channel_converter((int16_t *)buffers[buf_idx], fr_in,
					(int16_t *)buffers[buf_idx + 1]);
		buf_idx++;
	}

	/* Then SRC. */
	if (conv->speex_state != NULL) {
		fr_out = cras_frames_at_rate(conv->in_fmt.frame_rate,
					     fr_in,
					     conv->out_fmt.frame_rate);
		speex_resampler_process_interleaved_int(
				conv->speex_state,
				(int16_t *)buffers[buf_idx],
				&fr_in,
				(int16_t *)out_buf,
				&fr_out);
	}
	return fr_out;
}
