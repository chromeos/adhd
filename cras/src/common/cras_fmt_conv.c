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

/* The quality level is a value between 0 and 10. This is a tradeoff between
 * performance, latency, and quality. */
#define SPEEX_QUALITY_LEVEL 4
/* Max number of converters, src, down/up mix, and format. */
#define MAX_NUM_CONVERTERS 3
/* Channel index for stereo. */
#define STEREO_L 0
#define STEREO_R 1

typedef void (*sample_format_converter_t)(const uint8_t *in,
					  size_t in_samples,
					  uint8_t *out);
typedef size_t (*channel_converter_t)(struct cras_fmt_conv *conv,
				      const int16_t *in,
				      size_t in_frames,
				      int16_t *out);

/* Member data for the resampler. */
struct cras_fmt_conv {
	SpeexResamplerState *speex_state;
	channel_converter_t channel_converter;
	float **ch_conv_mtx; /* Coefficient matrix for mixing channels. */
	sample_format_converter_t sample_format_converter;
	struct cras_audio_format in_fmt;
	struct cras_audio_format out_fmt;
	uint8_t *tmp_bufs[MAX_NUM_CONVERTERS - 1];
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
				uint8_t *out)
{
	size_t i;
	uint16_t *_out = (uint16_t *)out;

	for (i = 0; i < in_samples; i++, in++, _out++)
		*_out = ((int16_t)*in - 0x80) << 8;
}

/* Converts from S24 to S16. */
static void convert_s24le_to_s16le(const uint8_t *in, size_t in_samples,
				   uint8_t *out)
{
	size_t i;
	int32_t *_in = (int32_t *)in;
	uint16_t *_out = (uint16_t *)out;

	for (i = 0; i < in_samples; i++, _in++, _out++)
		*_out = (int16_t)((*_in & 0x00ffffff) >> 8);
}

/* Converts from S32 to S16. */
static void convert_s32le_to_s16le(const uint8_t *in, size_t in_samples,
				   uint8_t *out)
{
	size_t i;
	int32_t *_in = (int32_t *)in;
	uint16_t *_out = (uint16_t *)out;

	for (i = 0; i < in_samples; i++, _in++, _out++)
		*_out = (int16_t)(*_in >> 16);
}

/* Converts from S16 to U8. */
static void convert_s16le_to_u8(const uint8_t *in, size_t in_samples,
				uint8_t *out)
{
	size_t i;
	int16_t *_in = (int16_t *)in;

	for (i = 0; i < in_samples; i++, _in++, out++)
		*out = (uint8_t)(*_in >> 8) + 128;
}

/* Converts from S16 to S24. */
static void convert_s16le_to_s24le(const uint8_t *in, size_t in_samples,
				   uint8_t *out)
{
	size_t i;
	int16_t *_in = (int16_t *)in;
	uint32_t *_out = (uint32_t *)out;

	for (i = 0; i < in_samples; i++, _in++, _out++)
		*_out = ((int32_t)*_in << 8);
}

/* Converts from S16 to S32. */
static void convert_s16le_to_s32le(const uint8_t *in, size_t in_samples,
				   uint8_t *out)
{
	size_t i;
	int16_t *_in = (int16_t *)in;
	uint32_t *_out = (uint32_t *)out;

	for (i = 0; i < in_samples; i++, _in++, _out++)
		*_out = ((int32_t)*_in << 16);
}

/*
 * Convert between different channel numbers.
 */

/* Converts S16 mono to S16 stereo. The out buffer must be double the size of
 * the input buffer. */
static size_t s16_mono_to_stereo(struct cras_fmt_conv *conv,
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

/* Converts S16 Stereo to S16 mono.  The output buffer only need be big enough
 * for mono samples. */
static size_t s16_stereo_to_mono(struct cras_fmt_conv *conv,
				const int16_t *in, size_t in_frames,
				int16_t *out)
{
	size_t i;

	for (i = 0; i < in_frames; i++)
		out[i] = s16_add_and_clip(in[2 * i], in[2 * i + 1]);
	return in_frames;
}

/* Converts S16 5.1 to S16 stereo. The out buffer can have room for just
 * stereo samples. This convert function is used as the default behavior
 * when channel layout is not set from the client side. */
static size_t s16_51_to_stereo(struct cras_fmt_conv *conv,
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

/* Multiplies buffer vector with coefficient vector. */
static int16_t multiply_buf_with_coef(float *coef,
				      const int16_t *buf,
				      size_t size)
{
	int32_t sum = 0;
	int i;

	for (i = 0; i < size; i++)
		sum += coef[i] * buf[i];
	sum = max(sum, -0x8000);
	sum = min(sum, 0x7fff);
	return (int16_t)sum;
}

static void normalize_buf(float *buf, size_t size)
{
	int i;
	float squre_sum = 0.0;
	for (i = 0; i < size; i++)
		squre_sum += buf[i] * buf[i];
	for (i = 0; i < size; i ++)
		buf[i] /= squre_sum;
}

/* Converts channels based on the channel conversion
 * coefficient matrix.
 */
static size_t convert_channels(struct cras_fmt_conv *conv,
			       const int16_t *in,
			       size_t in_frames,
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

static void free_channel_conv_matrix(float **p, size_t out_ch)
{
	int i;
	for (i = 0; i < out_ch; i ++)
		free(p[i]);
	free(p);
}


static float** alloc_channel_conv_matrix(size_t in_ch, size_t out_ch)
{
	int i;
	float **p;
	p = (float **)calloc(out_ch, sizeof(*p));
	if (p == NULL)
		return NULL;
	for (i = 0; i < out_ch; i++) {
		p[i] = (float *)calloc(in_ch, sizeof(*p[i]));
		if (p[i] == NULL)
			goto alloc_err;
	}
	return p;

alloc_err:
	if (p)
		free_channel_conv_matrix(p, out_ch);
	return NULL;
}

/* Populates the down mix matrix by rules:
 * 1. Front/side left(right) channel will mix to left(right) of
 *    full scale.
 * 2. Center and LFE will be split equally to left and right.
 *    Rear
 * 3. Rear left/right will split 1/4 of the power to opposite
 *    channel.
 */
static void surround51_to_stereo_downmix_mtx(float **mtx,
					     int8_t layout[CRAS_CH_MAX])
{
	if (layout[CRAS_CH_FC] != -1) {
		mtx[STEREO_L][layout[CRAS_CH_FC]] = 0.707;
		mtx[STEREO_R][layout[CRAS_CH_FC]] = 0.707;
	}
	if (layout[CRAS_CH_FL] != -1 && layout[CRAS_CH_FR] != -1) {
		mtx[STEREO_L][layout[CRAS_CH_FL]] = 1.0;
		mtx[STEREO_R][layout[CRAS_CH_FR]] = 1.0;
	}
	if (layout[CRAS_CH_SL] != -1 && layout[CRAS_CH_SR] != -1) {
		mtx[STEREO_L][layout[CRAS_CH_SL]] = 1.0;
		mtx[STEREO_R][layout[CRAS_CH_SR]] = 1.0;
	}
	if (layout[CRAS_CH_RL] != -1 && layout[CRAS_CH_RR] != -1) {
		/* Split 1/4 power to the other side */
		mtx[STEREO_L][layout[CRAS_CH_RL]] = 0.866;
		mtx[STEREO_R][layout[CRAS_CH_RL]] = 0.5;
		mtx[STEREO_R][layout[CRAS_CH_RR]] = 0.866;
		mtx[STEREO_L][layout[CRAS_CH_RR]] = 0.5;
	}
	if (layout[CRAS_CH_LFE] != -1) {
		mtx[STEREO_L][layout[CRAS_CH_LFE]] = 0.707;
		mtx[STEREO_R][layout[CRAS_CH_LFE]] = 0.707;
	}

	normalize_buf(mtx[STEREO_L], 6);
	normalize_buf(mtx[STEREO_R], 6);
}

/*
 * Exported interface
 */

struct cras_fmt_conv *cras_fmt_conv_create(const struct cras_audio_format *in,
					   const struct cras_audio_format *out,
					   size_t max_frames)
{
	struct cras_fmt_conv *conv;
	int rc;
	unsigned i;

	/* Only support conversion to/from S16LE samples. */
	if (out->format != SND_PCM_FORMAT_S16_LE &&
	    in->format != SND_PCM_FORMAT_S16_LE) {
		syslog(LOG_WARNING, "Invalid conversion %d %d",
		       in->format, out->format);
		return NULL;
	}

	conv = calloc(1, sizeof(*conv));
	if (conv == NULL)
		return NULL;
	conv->in_fmt = *in;
	conv->out_fmt = *out;

	/* Set up sample format conversion. */
	if (in->format != SND_PCM_FORMAT_S16_LE) {
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
			syslog(LOG_WARNING, "Invalid format %d", in->format);
			cras_fmt_conv_destroy(conv);
			return NULL;
		}
	} else if (out->format != SND_PCM_FORMAT_S16_LE) {
		conv->num_converters++;
		syslog(LOG_DEBUG, "Convert from format %d to %d.",
		       in->format, out->format);
		switch (out->format) {
		case SND_PCM_FORMAT_U8:
			conv->sample_format_converter = convert_s16le_to_u8;
			break;
		case SND_PCM_FORMAT_S24_LE:
			conv->sample_format_converter = convert_s16le_to_s24le;
			break;
		case SND_PCM_FORMAT_S32_LE:
			conv->sample_format_converter = convert_s16le_to_s32le;
			break;
		default:
			syslog(LOG_WARNING, "Invalid format %d", out->format);
			cras_fmt_conv_destroy(conv);
			return NULL;
		}
	}

	/* Set up channel number conversion. */
	if (in->num_channels != out->num_channels) {
		conv->num_converters++;
		syslog(LOG_DEBUG, "Convert from %zu to %zu channels.",
		       in->num_channels, out->num_channels);

		/* Populate the conversion matrix base on in/out channel count
		 * and layout. */
		if (in->num_channels == 1 && out->num_channels == 2) {
			conv->channel_converter = s16_mono_to_stereo;
		} else if (in->num_channels == 2 && out->num_channels == 1) {
			conv->channel_converter = s16_stereo_to_mono;
		} else if (in->num_channels == 6 && out->num_channels == 2) {
			int in_channel_layout_set = 0;

			/* Checks if channel_layout is set in the incoming format */
			for (i = 0; i < CRAS_CH_MAX; i++)
				if (in->channel_layout[i] != -1)
					in_channel_layout_set = 1;

			/* Use the conversion matrix based converter when a
			 * channel layout is set, or default to use existing
			 * converter to downmix to stereo */
			if (in_channel_layout_set) {
				conv->ch_conv_mtx = alloc_channel_conv_matrix(
						in->num_channels,
						out->num_channels);
				if (conv->ch_conv_mtx == NULL) {
					cras_fmt_conv_destroy(conv);
					return NULL;
				}
				conv->channel_converter = convert_channels;
				surround51_to_stereo_downmix_mtx(
						conv->ch_conv_mtx,
						conv->in_fmt.channel_layout);
			} else {
				conv->channel_converter = s16_51_to_stereo;
			}
		} else {
			syslog(LOG_WARNING,
			       "Invalid channel conversion %zu to %zu",
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

	/* Need num_converters-1 temp buffers, the final converter renders
	 * directly into the output. */
	for (i = 0; i < conv->num_converters - 1; i++) {
		conv->tmp_bufs[i] = malloc(
			max_frames *
			4 * /* width in bytes largest format. */
			max(in->num_channels, out->num_channels));
		if (conv->tmp_bufs[i] == NULL) {
			cras_fmt_conv_destroy(conv);
			return NULL;
		}
	}

	assert(conv->num_converters <= MAX_NUM_CONVERTERS);

	return conv;
}

void cras_fmt_conv_destroy(struct cras_fmt_conv *conv)
{
	unsigned i;
	if (conv->ch_conv_mtx)
		free_channel_conv_matrix(conv->ch_conv_mtx,
					 conv->out_fmt.num_channels);
	if (conv->speex_state)
		speex_resampler_destroy(conv->speex_state);
	for (i = 0; i < MAX_NUM_CONVERTERS - 1; i++)
		free(conv->tmp_bufs[i]);
	free(conv);
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

size_t cras_fmt_conv_convert_frames(struct cras_fmt_conv *conv,
				    uint8_t *in_buf,
				    uint8_t *out_buf,
				    size_t in_frames,
				    size_t out_frames)
{
	uint32_t fr_in, fr_out;
	uint8_t *buffers[MAX_NUM_CONVERTERS + 1]; /* converters + out buffer. */
	size_t buf_idx = 0;
	static int logged_frames_dont_fit;

	assert(conv);

	/* If no SRC, then in_frames should = out_frames. */
	if (conv->speex_state == NULL) {
		fr_in = min(in_frames, out_frames);
		if (out_frames < in_frames && !logged_frames_dont_fit) {
			syslog(LOG_INFO,
			       "fmt_conv: %zu to %zu no SRC.",
			       in_frames,
			       out_frames);
			logged_frames_dont_fit = 1;
		}
	} else {
		fr_in = in_frames;
	}
	fr_out = fr_in;

	/* Set up a chain of buffers.  The output buffer of the first conversion
	 * is used as input to the second and so forth, ending in the output
	 * buffer. */
	buffers[0] = (uint8_t *)in_buf;
	if (conv->num_converters == 2) {
		buffers[1] = (uint8_t *)conv->tmp_bufs[0];
	} else if (conv->num_converters == 3) {
		buffers[1] = (uint8_t *)conv->tmp_bufs[0];
		buffers[2] = (uint8_t *)conv->tmp_bufs[1];
	}
	buffers[conv->num_converters] = out_buf;

	/* If the input format isn't S16_LE convert to it. */
	if (conv->in_fmt.format != SND_PCM_FORMAT_S16_LE) {
		conv->sample_format_converter(buffers[buf_idx],
					      fr_in * conv->in_fmt.num_channels,
					      (uint8_t *)buffers[buf_idx + 1]);
		buf_idx++;
	}

	/* Then channel conversion. */
	if (conv->channel_converter != NULL) {
		conv->channel_converter(conv,
					(int16_t *)buffers[buf_idx],
					fr_in,
					(int16_t *)buffers[buf_idx + 1]);
		buf_idx++;
	}

	/* Then SRC. */
	if (conv->speex_state != NULL) {
		fr_out = cras_frames_at_rate(conv->in_fmt.frame_rate,
					     fr_in,
					     conv->out_fmt.frame_rate);
		if (fr_out > out_frames + 1 && !logged_frames_dont_fit) {
			syslog(LOG_INFO,
			       "fmt_conv: put %u frames in %zu sized buffer",
			       fr_out,
			       out_frames);
			logged_frames_dont_fit = 1;
		}
		/* limit frames to the output size. */
		fr_out = min(fr_out, out_frames);
		speex_resampler_process_interleaved_int(
				conv->speex_state,
				(int16_t *)buffers[buf_idx],
				&fr_in,
				(int16_t *)out_buf,
				&fr_out);
	}

	/* If the output format isn't S16_LE convert to it. */
	if (conv->out_fmt.format != SND_PCM_FORMAT_S16_LE) {
		conv->sample_format_converter(
				buffers[buf_idx],
				fr_in * conv->out_fmt.num_channels,
				(uint8_t *)buffers[buf_idx + 1]);
		buf_idx++;
	}

	return fr_out;
}

int cras_fmt_conversion_needed(const struct cras_audio_format *a,
			       const struct cras_audio_format *b)
{
	return (a->format != b->format ||
		a->num_channels != b->num_channels ||
		a->frame_rate != b->frame_rate);
}
