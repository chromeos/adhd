/* Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* For now just use speex, can add more resamplers later. */
#include <speex/speex_resampler.h>

#include "cras_fmt_conv.h"
#include "cras_types.h"
#include "cras_util.h"

/* Want the fastest conversion we can get. */
#define SPEEX_QUALITY_LEVEL 0

/* Member data for the resampler. */
struct cras_fmt_conv {
	SpeexResamplerState *speex_state;
	struct cras_audio_format in_fmt;
	struct cras_audio_format out_fmt;
	uint8_t *buf;
};

/*
 * External interface
 */

struct cras_fmt_conv *cras_fmt_conv_create(const struct cras_audio_format *in,
					   const struct cras_audio_format *out,
					   size_t max_frames)
{
	struct cras_fmt_conv *conv;
	int rc;

	/* Don't support format conversion or up/down sampling(yet).
	 * Only support S16 samples */
	if (in->format != out->format ||
	    in->num_channels != out->num_channels ||
	    in->format != SND_PCM_FORMAT_S16_LE)
		return NULL;

	conv = malloc(sizeof(*conv));
	if (conv == NULL)
		return NULL;

	conv->buf = malloc(max_frames * cras_get_format_bytes(in));
	if (conv->buf == NULL) {
		free(conv);
		return NULL;
	}

	conv->in_fmt = *in;
	conv->out_fmt = *out;

	conv->speex_state = speex_resampler_init(in->num_channels,
						 in->frame_rate,
						 out->frame_rate,
						 SPEEX_QUALITY_LEVEL,
						 &rc);
	if (conv->speex_state == NULL) {
		cras_fmt_conv_destroy(conv);
		return NULL;
	}
	return conv;
}

void cras_fmt_conv_destroy(struct cras_fmt_conv *conv)
{
	if (conv->speex_state)
		speex_resampler_destroy(conv->speex_state);
	free(conv->buf);
	free(conv);
}

uint8_t *cras_fmt_conv_get_buffer(struct cras_fmt_conv *conv)
{
	return conv->buf;
}

size_t cras_fmt_conv_in_frames_to_out(struct cras_fmt_conv *conv,
				      size_t in_frames)
{
	return cras_frames_at_rate(conv->in_fmt.frame_rate, in_frames,
				   conv->out_fmt.frame_rate);
}

size_t cras_fmt_conv_out_frames_to_in(struct cras_fmt_conv *conv,
				      size_t out_frames)
{
	return cras_frames_at_rate(conv->out_fmt.frame_rate, out_frames,
				   conv->in_fmt.frame_rate);
}

size_t cras_fmt_conv_convert_to(struct cras_fmt_conv *conv, uint8_t *out_buf,
				size_t in_frames)
{
	uint32_t fr_in, fr_out;

	fr_in = in_frames;
	fr_out = cras_fmt_conv_in_frames_to_out(conv, fr_in);
	/* Enforced 16bit samples in create, when other formats are supported,
	 * expand this to call the correct function and cast appropriately based
	 * on the format. */
	speex_resampler_process_interleaved_int(conv->speex_state,
						(int16_t *)conv->buf, &fr_in,
						(int16_t *)out_buf, &fr_out);
	return fr_out;
}
