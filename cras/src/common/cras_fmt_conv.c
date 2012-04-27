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

typedef size_t (*channel_converter_t)(const int16_t *in, size_t in_frames,
				      int16_t *out);

/* Member data for the resampler. */
struct cras_fmt_conv {
	SpeexResamplerState *speex_state;
	channel_converter_t channel_converter;
	struct cras_audio_format in_fmt;
	struct cras_audio_format out_fmt;
	uint8_t *tmp_buf; /* Only needed if changing channels and doing SRC. */
	uint8_t *input_buf;
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
	channel_converter_t channel_converter = NULL;
	int rc;

	/* Don't support format conversion(yet).
	 * Only support S16 samples. */
	if (in->format != out->format ||
	    in->format != SND_PCM_FORMAT_S16_LE) {
		syslog(LOG_ERR, "Invalid format %d", in->format);
		return NULL;
	}
	/* Only support Stero to Mono Conversion. */
	if (in->num_channels != out->num_channels) {
		if (in->num_channels == 1 && out->num_channels == 2) {
			channel_converter = s16_mono_to_stereo;
		} else if (in->num_channels == 6 && out->num_channels == 2) {
			channel_converter = s16_51_to_stereo;
		} else {
			syslog(LOG_ERR, "Invalid channel conversion %zu to %zu",
			       in->num_channels, out->num_channels);
			return NULL;
		}
	}

	conv = calloc(1, sizeof(*conv));
	if (conv == NULL)
		return NULL;

	conv->input_buf = malloc(max_frames * cras_get_format_bytes(in));
	if (conv->input_buf == NULL) {
		free(conv);
		return NULL;
	}

	conv->in_fmt = *in;
	conv->out_fmt = *out;
	conv->channel_converter = channel_converter;

	if (in->frame_rate != out->frame_rate) {
		conv->speex_state = speex_resampler_init(in->num_channels,
							 in->frame_rate,
							 out->frame_rate,
							 SPEEX_QUALITY_LEVEL,
							 &rc);
		if (conv->speex_state == NULL) {
			syslog(LOG_ERR, "Fail to create speex:%zu %zu %zu %d",
			       in->num_channels,
			       in->frame_rate,
			       out->frame_rate,
			       rc);
			cras_fmt_conv_destroy(conv);
			return NULL;
		}

		if (conv->channel_converter) {
			/* Will need a temporary area to stage before SRC. */
			conv->tmp_buf =
				malloc(max_frames * cras_get_format_bytes(out));
			if (conv->tmp_buf == NULL) {
				cras_fmt_conv_destroy(conv);
				return NULL;
			}
		}
	}
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
	/* Enforced 16bit samples in create, when other formats are supported,
	 * expand this to call the correct function and cast appropriately based
	 * on the format. */
	uint32_t fr_in, fr_out;
	uint8_t *src_in_buf = conv->input_buf;

	fr_in = in_frames;
	fr_out = fr_in;

	/* Start with channel conversion. */
	if (conv->channel_converter != NULL) {
		uint8_t *chan_out_buf = out_buf;
		if (conv->tmp_buf != NULL)
			chan_out_buf = conv->tmp_buf;
		conv->channel_converter((int16_t *)conv->input_buf, fr_in,
					(int16_t *)chan_out_buf);
		src_in_buf = conv->tmp_buf;
	}

	/* Then SRC. */
	if (conv->speex_state != NULL) {
		fr_out = cras_frames_at_rate(conv->in_fmt.frame_rate,
					     fr_in,
					     conv->out_fmt.frame_rate);
		speex_resampler_process_interleaved_int(conv->speex_state,
							(int16_t *)src_in_buf,
							&fr_in,
							(int16_t *)out_buf,
							&fr_out);
	}
	return fr_out;
}
