/* Copyright (c) 2013 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <fpu_control.h>
#include "dsp_util.h"

#ifndef max
#define max(a, b) ({ __typeof__(a) _a = (a);	\
			__typeof__(b) _b = (b);	\
			_a > _b ? _a : _b; })
#endif

#ifndef min
#define min(a, b) ({ __typeof__(a) _a = (a);	\
			__typeof__(b) _b = (b);	\
			_a < _b ? _a : _b; })
#endif

#ifdef __ARM_NEON__
#include <arm_neon.h>

static void deinterleave_stereo_neon(int16_t *input, float *output1,
				     float *output2, int frames)
{
	/* Process 4 frames (8 samples) each loop. */
	/* L0 R0 L1 R1, L2 R2 L3 R3 -> L0 L1 L2 L3, R0 R1 R2 R3 */
	int chunk = frames >> 2;
	frames &= 3;
	while (chunk--) {
		int32x4_t c = vmovl_s16(*(int16x4_t *)input);
		int32x4_t d = vmovl_s16(*(int16x4_t *)(input + 4));
		float32x4_t e = vcvtq_n_f32_s32(c, 15);
		float32x4_t f = vcvtq_n_f32_s32(d, 15);
		float32x4x2_t g = vuzpq_f32(e, f);
		*(float32x4_t *)output1 = g.val[0];
		*(float32x4_t *)output2 = g.val[1];
		input += 8;
		output1 += 4;
		output2 += 4;
	}

	/* The remaining samples. */
	while (frames--) {
		*output1++ = *input++ / 32768.0f;
		*output2++ = *input++ / 32768.0f;
	}
}

static void interleave_stereo_neon(float *input1, float *input2,
				   int16_t *output, int frames)
{
	/* Process 4 frames (8 samples) each loop. */
	/* L0 L1 L2 L3, R0 R1 R2 R3 -> L0 R0 L1 R1, L2 R2 L3 R3 */
	float32x4_t zero = vdupq_n_f32(0.0f);
	float32x4_t pos = vdupq_n_f32(0.5f / 32768.0f);
	float32x4_t neg = vdupq_n_f32(-0.5f / 32768.0f);
	int32x4_t k = vdupq_n_s32(-32768);
	int32x4_t l = vdupq_n_s32(32767);
	int chunk = frames >> 2;
	frames &= 3;
	while (chunk--) {
		float32x4_t a = *(float32x4_t *)input1;
		float32x4_t b = *(float32x4_t *)input2;
		/* We try to round to the nearest number by adding 0.5
		 * to positive input, and adding -0.5 to the negative
		 * input, then truncate.
		 */
		a += vbslq_f32(vcgtq_f32(a, zero), pos, neg);
		b += vbslq_f32(vcgtq_f32(b, zero), pos, neg);
		int32x4_t q = vcvtq_n_s32_f32(a, 15);
		int32x4_t r = vcvtq_n_s32_f32(b, 15);
		int16x4_t m = vqmovn_s32(vminq_s32(vmaxq_s32(q, k), l));
		int16x4_t n = vqmovn_s32(vminq_s32(vmaxq_s32(r, k), l));
		*(int16x4x2_t *)output = vzip_s16(m, n);
		input1 += 4;
		input2 += 4;
		output += 8;
	}

	/* The remaining samples */
	while (frames--) {
		float f;
		f = *input1++;
		f += (f > 0) ? (0.5f / 32768.0f) : (-0.5f / 32768.0f);
		*output++ = max(-32768, min(32767, (int)(f * 32768.0f)));
		f = *input2++;
		f += (f > 0) ? (0.5f / 32768.0f) : (-0.5f / 32768.0f);
		*output++ = max(-32768, min(32767, (int)(f * 32768.0f)));
	}
}

#endif

void dsp_util_deinterleave(int16_t *input, float *const *output, int channels,
			   int frames)
{
	float *output_ptr[channels];
	int i, j;

#ifdef __ARM_NEON__
	if (channels == 2) {
		deinterleave_stereo_neon(input, output[0], output[1], frames);
		return;
	}
#endif

	for (i = 0; i < channels; i++)
		output_ptr[i] = output[i];

	for (i = 0; i < frames; i++)
		for (j = 0; j < channels; j++)
			*(output_ptr[j]++) = *input++ / 32768.0f;
}

void dsp_util_interleave(float *const *input, int16_t *output, int channels,
			 int frames)
{
	float *input_ptr[channels];
	int i, j;

#ifdef __ARM_NEON__
	if (channels == 2) {
		interleave_stereo_neon(input[0], input[1], output, frames);
		return;
	}
#endif

	for (i = 0; i < channels; i++)
		input_ptr[i] = input[i];

	for (i = 0; i < frames; i++)
		for (j = 0; j < channels; j++) {
			int16_t i16;
			float f = *(input_ptr[j]++) * 32768.0f;
			if (f > 32767)
				i16 = 32767;
			else if (f < -32768)
				i16 = -32768;
			else
				i16 = (int16_t) (f > 0 ? f + 0.5f : f - 0.5f);
			*output++ = i16;
		}
}

void dsp_enable_flush_denormal_to_zero()
{
#if defined(__i386__) || defined(__x86_64__)
	unsigned int mxcsr;
	mxcsr = __builtin_ia32_stmxcsr();
	__builtin_ia32_ldmxcsr(mxcsr | 0x8040);
#elif defined(__arm__)
	int cw;
	_FPU_GETCW(cw);
	_FPU_SETCW(cw | (1 << 24));
#else
#warning "Don't know how to disable denorms. Performace may suffer."
#endif
}
