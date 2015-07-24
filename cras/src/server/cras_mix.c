/* Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <stdint.h>

#include "cras_system_state.h"

#define MAX_VOLUME_TO_SCALE 0.9999999
#define MIN_VOLUME_TO_SCALE 0.0000001

/*
 * Signed 16 bit little endian functions.
 */

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

void cras_mix_add_stride_s16_le(uint8_t *dst, uint8_t *src,
				unsigned int dst_stride,
				unsigned int src_stride,
				unsigned int count)
{
	unsigned int i;

	for (i = 0; i < count; i++) {
		int32_t sum;
		sum = *(int16_t *)dst + *(int16_t *)src;
		if (sum > INT16_MAX)
			sum = INT16_MAX;
		else if (sum < INT16_MIN)
			sum = INT16_MIN;
		*(int16_t*)dst = sum;
		dst += dst_stride;
		src += src_stride;
	}
}

/*
 * Signed 24 bit little endian functions.
 */

static void cras_mix_add_clip_s24_le(int32_t *dst,
				     const int32_t *src,
				     size_t count)
{
	int32_t sum;
	size_t i;

	for (i = 0; i < count; i++) {
		sum = dst[i] + src[i];
		if (sum > 0x007fffff)
			sum = 0x007fffff;
		else if (sum < (int32_t)0xff800000)
			sum = (int32_t)0xff800000;
		dst[i] = sum;
	}
}

/* Adds src into dst, after scaling by vol.
 * Just hard limits to the min and max S24 value, can be improved later. */
static void scale_add_clip_s24_le(int32_t *dst,
				  const int32_t *src,
				  size_t count,
				  float vol)
{
	int32_t sum;
	size_t i;

	if (vol > MAX_VOLUME_TO_SCALE)
		return cras_mix_add_clip_s24_le(dst, src, count);

	for (i = 0; i < count; i++) {
		sum = dst[i] + (int32_t)(src[i] * vol);
		if (sum > 0x007fffff)
			sum = 0x007fffff;
		else if (sum < (int32_t)0xff800000)
			sum = (int32_t)0xff800000;
		dst[i] = sum;
	}
}

/* Adds the first stream to the mix.  Don't need to mix, just setup to the new
 * values. If volume is 1.0, just memcpy. */
static void copy_scaled_s24_le(int32_t *dst,
			       const int32_t *src,
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

static void cras_scale_buffer_s24_le(uint8_t *buffer, unsigned int count,
				     float scaler)
{
	int i;
	int32_t *out = (int32_t *)buffer;

	if (scaler > MAX_VOLUME_TO_SCALE)
		return;

	if (scaler < MIN_VOLUME_TO_SCALE) {
		memset(out, 0, count * sizeof(*out));
		return;
	}

	for (i = 0; i < count; i++)
		out[i] *= scaler;
}

static void cras_mix_add_s24_le(uint8_t *dst, uint8_t *src,
				unsigned int count, unsigned int index,
				int mute, float mix_vol)
{
	int32_t *out = (int32_t *)dst;
	int32_t *in = (int32_t *)src;

	if (mute || (mix_vol < MIN_VOLUME_TO_SCALE)) {
		if (index == 0)
			memset(out, 0, count * sizeof(*out));
		return;
	}

	if (index == 0)
		return copy_scaled_s24_le(out, in, count, mix_vol);

	scale_add_clip_s24_le(out, in, count, mix_vol);
}

void cras_mix_add_stride_s24_le(uint8_t *dst, uint8_t *src,
				unsigned int dst_stride,
				unsigned int src_stride,
				unsigned int count)
{
	unsigned int i;

	for (i = 0; i < count; i++) {
		int32_t sum;
		sum = *(int32_t *)dst + *(int32_t *)src;
		if (sum > 0x007fffff)
			sum = 0x007fffff;
		else if (sum < (int32_t)0xff800000)
			sum = (int32_t)0xff800000;
		*(int32_t*)dst = sum;
		dst += dst_stride;
		src += src_stride;
	}
}

/*
 * Signed 32 bit little endian functions.
 */

static void cras_mix_add_clip_s32_le(int32_t *dst,
				     const int32_t *src,
				     size_t count)
{
	int64_t sum;
	size_t i;

	for (i = 0; i < count; i++) {
		sum = (int64_t)dst[i] + (int64_t)src[i];
		if (sum > INT32_MAX)
			sum = INT32_MAX;
		else if (sum < INT32_MIN)
			sum = INT32_MIN;
		dst[i] = sum;
	}
}

/* Adds src into dst, after scaling by vol.
 * Just hard limits to the min and max S32 value, can be improved later. */
static void scale_add_clip_s32_le(int32_t *dst,
				  const int32_t *src,
				  size_t count,
				  float vol)
{
	int64_t sum;
	size_t i;

	if (vol > MAX_VOLUME_TO_SCALE)
		return cras_mix_add_clip_s32_le(dst, src, count);

	for (i = 0; i < count; i++) {
		sum = (int64_t)dst[i] + (int64_t)(src[i] * vol);
		if (sum > INT32_MAX)
			sum = INT32_MAX;
		else if (sum < INT32_MIN)
			sum = INT32_MIN;
		dst[i] = sum;
	}
}

/* Adds the first stream to the mix.  Don't need to mix, just setup to the new
 * values. If volume is 1.0, just memcpy. */
static void copy_scaled_s32_le(int32_t *dst,
			       const int32_t *src,
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

static void cras_scale_buffer_s32_le(uint8_t *buffer, unsigned int count,
				     float scaler)
{
	int i;
	int32_t *out = (int32_t *)buffer;

	if (scaler > MAX_VOLUME_TO_SCALE)
		return;

	if (scaler < MIN_VOLUME_TO_SCALE) {
		memset(out, 0, count * sizeof(*out));
		return;
	}

	for (i = 0; i < count; i++)
		out[i] *= scaler;
}

static void cras_mix_add_s32_le(uint8_t *dst, uint8_t *src,
				unsigned int count, unsigned int index,
				int mute, float mix_vol)
{
	int32_t *out = (int32_t *)dst;
	int32_t *in = (int32_t *)src;

	if (mute || (mix_vol < MIN_VOLUME_TO_SCALE)) {
		if (index == 0)
			memset(out, 0, count * sizeof(*out));
		return;
	}

	if (index == 0)
		return copy_scaled_s32_le(out, in, count, mix_vol);

	scale_add_clip_s32_le(out, in, count, mix_vol);
}

void cras_mix_add_stride_s32_le(uint8_t *dst, uint8_t *src,
				unsigned int dst_stride,
				unsigned int src_stride,
				unsigned int count)
{
	unsigned int i;

	for (i = 0; i < count; i++) {
		int64_t sum;
		sum = *(int32_t *)dst + *(int32_t *)src;
		if (sum > INT32_MAX)
			sum = INT32_MAX;
		else if (sum < INT32_MIN)
			sum = INT32_MIN;
		*(int32_t*)dst = sum;
		dst += dst_stride;
		src += src_stride;
	}
}

/*
 * Signed 24 bit little endian in three bytes functions.
 */

/* Convert 3bytes Signed 24bit integer to a Signed 32bit integer.
 * Just a helper function. */
static inline void convert_single_s243le_to_s32le(int32_t *dst,
						  const uint8_t *src)
{
	*dst = 0;
	memcpy((uint8_t *)dst + 1, src, 3);
}

static inline void convert_single_s32le_to_s243le(uint8_t *dst,
						  const int32_t *src)
{
	memcpy(dst, (uint8_t *)src + 1, 3);
}

static void cras_mix_add_clip_s24_3le(uint8_t *dst,
				      const uint8_t *src,
				      size_t count)
{
	int64_t sum;
	int32_t dst_frame;
	int32_t src_frame;
	size_t i;

	for (i = 0; i < count; i++, dst += 3, src += 3) {
		convert_single_s243le_to_s32le(&dst_frame, dst);
		convert_single_s243le_to_s32le(&src_frame, src);
		sum = (int64_t)dst_frame + (int64_t)src_frame;
		if (sum > INT32_MAX)
			sum = INT32_MAX;
		else if (sum < INT32_MIN)
			sum = INT32_MIN;
		dst_frame = (int32_t)sum;
		convert_single_s32le_to_s243le(dst, &dst_frame);
	}
}

/* Adds src into dst, after scaling by vol.
 * Just hard limits to the min and max S24 value, can be improved later. */
static void scale_add_clip_s24_3le(uint8_t *dst,
				   const uint8_t *src,
				   size_t count,
				   float vol)
{
	int64_t sum;
	int32_t dst_frame;
	int32_t src_frame;
	size_t i;

	if (vol > MAX_VOLUME_TO_SCALE)
		return cras_mix_add_clip_s24_3le(dst, src, count);

	for (i = 0; i < count; i++, dst += 3, src += 3) {
		convert_single_s243le_to_s32le(&dst_frame, dst);
		convert_single_s243le_to_s32le(&src_frame, src);
		sum = (int64_t)dst_frame + (int64_t)(src_frame * vol);
		if (sum > INT32_MAX)
			sum = INT32_MAX;
		else if (sum < INT32_MIN)
			sum = INT32_MIN;
		dst_frame = (int32_t)sum;
		convert_single_s32le_to_s243le(dst, &dst_frame);
	}
}

/* Adds the first stream to the mix.  Don't need to mix, just setup to the new
 * values. If volume is 1.0, just memcpy. */
static void copy_scaled_s24_3le(uint8_t *dst,
			        const uint8_t *src,
			        size_t count,
			        float volume_scaler)
{
	int32_t frame;
	size_t i;

	if (volume_scaler > MAX_VOLUME_TO_SCALE) {
		memcpy(dst, src, 3 * count * sizeof(*src));
		return;
	}

	for (i = 0; i < count; i++, dst += 3, src += 3) {
		convert_single_s243le_to_s32le(&frame, src);
		frame *= volume_scaler;
		convert_single_s32le_to_s243le(dst, &frame);
	}
}

static void cras_scale_buffer_s24_3le(uint8_t *buffer, unsigned int count,
				      float scaler)
{
	int32_t frame;
	int i;

	if (scaler > MAX_VOLUME_TO_SCALE)
		return;

	if (scaler < MIN_VOLUME_TO_SCALE) {
		memset(buffer, 0, 3 * count * sizeof(*buffer));
		return;
	}

	for (i = 0; i < count; i++, buffer += 3) {
		convert_single_s243le_to_s32le(&frame, buffer);
		frame *= scaler;
		convert_single_s32le_to_s243le(buffer, &frame);
	}
}

static void cras_mix_add_s24_3le(uint8_t *dst, uint8_t *src,
				 unsigned int count, unsigned int index,
				 int mute, float mix_vol)
{
	uint8_t *out = dst;
	uint8_t *in = src;

	if (mute || (mix_vol < MIN_VOLUME_TO_SCALE)) {
		if (index == 0)
			memset(out, 0, 3 * count * sizeof(*out));
		return;
	}

	if (index == 0)
		return copy_scaled_s24_3le(out, in, count, mix_vol);

	scale_add_clip_s24_3le(out, in, count, mix_vol);
}

void cras_mix_add_stride_s24_3le(uint8_t *dst, uint8_t *src,
				 unsigned int dst_stride,
				 unsigned int src_stride,
				 unsigned int count)
{
	unsigned int i;
	int64_t sum;
	int32_t dst_frame;
	int32_t src_frame;

	for (i = 0; i < count; i++) {
		convert_single_s243le_to_s32le(&dst_frame, dst);
		convert_single_s243le_to_s32le(&src_frame, src);
		sum = (int64_t)dst_frame + (int64_t)src_frame;
		if (sum > INT32_MAX)
			sum = INT32_MAX;
		else if (sum < INT32_MIN)
			sum = INT32_MIN;
		dst_frame = (int32_t)sum;
		convert_single_s32le_to_s243le(dst, &dst_frame);
		dst += dst_stride;
		src += src_stride;
	}
}
/*
 * Exported Interface
 */

void cras_scale_buffer(snd_pcm_format_t fmt, uint8_t *buff, unsigned int count,
		       float scaler)
{
	switch (fmt) {
	case SND_PCM_FORMAT_S16_LE:
		return cras_scale_buffer_s16_le(buff, count, scaler);
	case SND_PCM_FORMAT_S24_LE:
		return cras_scale_buffer_s24_le(buff, count, scaler);
	case SND_PCM_FORMAT_S32_LE:
		return cras_scale_buffer_s32_le(buff, count, scaler);
	case SND_PCM_FORMAT_S24_3LE:
		return cras_scale_buffer_s24_3le(buff, count, scaler);
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
	case SND_PCM_FORMAT_S24_LE:
		return cras_mix_add_s24_le(dst, src, count, index, mute,
					   mix_vol);
	case SND_PCM_FORMAT_S32_LE:
		return cras_mix_add_s32_le(dst, src, count, index, mute,
					   mix_vol);
	case SND_PCM_FORMAT_S24_3LE:
		return cras_mix_add_s24_3le(dst, src, count, index, mute,
					    mix_vol);
	default:
		break;
	}
}

void cras_mix_add_stride(snd_pcm_format_t fmt, uint8_t *dst, uint8_t *src,
			 unsigned int count, unsigned int dst_stride,
			 unsigned int src_stride)
{
	switch (fmt) {
	case SND_PCM_FORMAT_S16_LE:
		return cras_mix_add_stride_s16_le(dst, src, dst_stride,
						  src_stride, count);
	case SND_PCM_FORMAT_S24_LE:
		return cras_mix_add_stride_s24_le(dst, src, dst_stride,
						  src_stride, count);
	case SND_PCM_FORMAT_S32_LE:
		return cras_mix_add_stride_s32_le(dst, src, dst_stride,
						  src_stride, count);
	case SND_PCM_FORMAT_S24_3LE:
		return cras_mix_add_stride_s24_3le(dst, src, dst_stride,
						   src_stride, count);
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
