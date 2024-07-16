/* Copyright 2013 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "cras/src/dsp/eq2.h"

#include <errno.h>
#include <stdlib.h>

#include "cras/src/dsp/biquad.h"
#include "cras/src/dsp/rust/headers/biquad.h"
#include "user/eq.h"

#define EQ2_NUM_CHANNELS 2

struct eq2 {
  int n[EQ2_NUM_CHANNELS];
  struct biquad biquad[MAX_BIQUADS_PER_EQ2][EQ2_NUM_CHANNELS];
};

struct eq2* eq2_new() {
  int i, j;
  struct eq2* eq2 = calloc(1, sizeof(*eq2));
  if (!eq2) {
    return NULL;
  }

  /* Initialize all biquads to identity filter, so if two channels have
   * different numbers of biquads, it still works. */
  for (i = 0; i < MAX_BIQUADS_PER_EQ2; i++) {
    for (j = 0; j < EQ2_NUM_CHANNELS; j++) {
      eq2->biquad[i][j] = biquad_new_set(BQ_NONE, 0, 0, 0);
    }
  }

  return eq2;
}

void eq2_free(struct eq2* eq2) {
  free(eq2);
}

int eq2_append_biquad(struct eq2* eq2,
                      int channel,
                      enum biquad_type type,
                      float freq,
                      float Q,
                      float gain) {
  if (eq2->n[channel] >= MAX_BIQUADS_PER_EQ2) {
    return -EINVAL;
  }
  eq2->biquad[eq2->n[channel]++][channel] = biquad_new_set(type, freq, Q, gain);
  return 0;
}

int eq2_append_biquad_direct(struct eq2* eq2,
                             int channel,
                             const struct biquad* biquad) {
  if (eq2->n[channel] >= MAX_BIQUADS_PER_EQ2) {
    return -EINVAL;
  }
  eq2->biquad[eq2->n[channel]++][channel] = *biquad;
  return 0;
}

static inline void eq2_process_one(struct biquad (*bq)[2],
                                   float* data0,
                                   float* data1,
                                   int count) {
  struct biquad* qL = &bq[0][0];
  struct biquad* qR = &bq[0][1];

  float x1L = qL->x1;
  float x2L = qL->x2;
  float y1L = qL->y1;
  float y2L = qL->y2;
  float b0L = qL->b0;
  float b1L = qL->b1;
  float b2L = qL->b2;
  float a1L = qL->a1;
  float a2L = qL->a2;

  float x1R = qR->x1;
  float x2R = qR->x2;
  float y1R = qR->y1;
  float y2R = qR->y2;
  float b0R = qR->b0;
  float b1R = qR->b1;
  float b2R = qR->b2;
  float a1R = qR->a1;
  float a2R = qR->a2;

  int j;
  for (j = 0; j < count; j++) {
    float xL = data0[j];
    float xR = data1[j];

    float yL = b0L * xL + b1L * x1L + b2L * x2L - a1L * y1L - a2L * y2L;
    x2L = x1L;
    x1L = xL;
    y2L = y1L;
    y1L = yL;

    float yR = b0R * xR + b1R * x1R + b2R * x2R - a1R * y1R - a2R * y2R;
    x2R = x1R;
    x1R = xR;
    y2R = y1R;
    y1R = yR;

    data0[j] = yL;
    data1[j] = yR;
  }

  qL->x1 = x1L;
  qL->x2 = x2L;
  qL->y1 = y1L;
  qL->y2 = y2L;
  qR->x1 = x1R;
  qR->x2 = x2R;
  qR->y1 = y1R;
  qR->y2 = y2R;
}

#ifdef __ARM_NEON__
#include <arm_neon.h>
static inline void eq2_process_two_neon(struct biquad (*bq)[2],
                                        float* data0,
                                        float* data1,
                                        int count) {
  struct biquad* qL = &bq[0][0];
  struct biquad* rL = &bq[1][0];
  struct biquad* qR = &bq[0][1];
  struct biquad* rR = &bq[1][1];

  float32x2_t x1 = {qL->x1, qR->x1};
  float32x2_t x2 = {qL->x2, qR->x2};
  float32x2_t y1 = {qL->y1, qR->y1};
  float32x2_t y2 = {qL->y2, qR->y2};
  float32x2_t qb0 = {qL->b0, qR->b0};
  float32x2_t qb1 = {qL->b1, qR->b1};
  float32x2_t qb2 = {qL->b2, qR->b2};
  float32x2_t qa1 = {qL->a1, qR->a1};
  float32x2_t qa2 = {qL->a2, qR->a2};

  float32x2_t z1 = {rL->y1, rR->y1};
  float32x2_t z2 = {rL->y2, rR->y2};
  float32x2_t rb0 = {rL->b0, rR->b0};
  float32x2_t rb1 = {rL->b1, rR->b1};
  float32x2_t rb2 = {rL->b2, rR->b2};
  float32x2_t ra1 = {rL->a1, rR->a1};
  float32x2_t ra2 = {rL->a2, rR->a2};

  // clang-format off
	__asm__ __volatile__(
		// d0 = x, d1 = y, d2 = z
		"1:                                     \n"
		"vmul.f32 d1, %P[qb1], %P[x1]           \n"
		"vld1.32 d0[0], [%[data0]]              \n"
		"vld1.32 d0[1], [%[data1]]              \n"
		"subs %[count], #1                      \n"
		"vmul.f32 d2, %P[rb1], %P[y1]           \n"
		"vmla.f32 d1, %P[qb0], d0               \n"
		"vmla.f32 d1, %P[qb2], %P[x2]           \n"
		"vmov.f32 %P[x2], %P[x1]                \n"
		"vmov.f32 %P[x1], d0                    \n"
		"vmls.f32 d1, %P[qa1], %P[y1]           \n"
		"vmls.f32 d1, %P[qa2], %P[y2]           \n"
		"vmla.f32 d2, %P[rb0], d1               \n"
		"vmla.f32 d2, %P[rb2], %P[y2]           \n"
		"vmov.f32 %P[y2], %P[y1]                \n"
		"vmov.f32 %P[y1], d1                    \n"
		"vmls.f32 d2, %P[ra1], %P[z1]           \n"
		"vmls.f32 d2, %P[ra2], %P[z2]           \n"
		"vmov.f32 %P[z2], %P[z1]                \n"
		"vmov.f32 %P[z1], d2                    \n"
		"vst1.f32 d2[0], [%[data0]]!            \n"
		"vst1.f32 d2[1], [%[data1]]!            \n"
		"bne 1b                                 \n"
		: // output
		  [data0]"+r"(data0),
		  [data1]"+r"(data1),
		  [count]"+r"(count),
		  [x1]"+w"(x1),
		  [x2]"+w"(x2),
		  [y1]"+w"(y1),
		  [y2]"+w"(y2),
		  [z1]"+w"(z1),
		  [z2]"+w"(z2)
		: // input
		  [qb0]"w"(qb0),
		  [qb1]"w"(qb1),
		  [qb2]"w"(qb2),
		  [qa1]"w"(qa1),
		  [qa2]"w"(qa2),
		  [rb0]"w"(rb0),
		  [rb1]"w"(rb1),
		  [rb2]"w"(rb2),
		  [ra1]"w"(ra1),
		  [ra2]"w"(ra2)
		: // clobber
		  "d0", "d1", "d2", "memory", "cc");
  // clang-format on

  qL->x1 = x1[0];
  qL->x2 = x2[0];
  qL->y1 = y1[0];
  qL->y2 = y2[0];
  rL->y1 = z1[0];
  rL->y2 = z2[0];
  qR->x1 = x1[1];
  qR->x2 = x2[1];
  qR->y1 = y1[1];
  qR->y2 = y2[1];
  rR->y1 = z1[1];
  rR->y2 = z2[1];
}
#endif

#if defined(__SSE3__) && defined(__x86_64__)
#include <emmintrin.h>
static inline void eq2_process_two_sse3(struct biquad (*bq)[2],
                                        float* data0,
                                        float* data1,
                                        int count) {
  struct biquad* qL = &bq[0][0];
  struct biquad* rL = &bq[1][0];
  struct biquad* qR = &bq[0][1];
  struct biquad* rR = &bq[1][1];

  __m128 x1 = {qL->x1, qR->x1};
  __m128 x2 = {qL->x2, qR->x2};
  __m128 y1 = {qL->y1, qR->y1};
  __m128 y2 = {qL->y2, qR->y2};
  __m128 qb0 = {qL->b0, qR->b0};
  __m128 qb1 = {qL->b1, qR->b1};
  __m128 qb2 = {qL->b2, qR->b2};
  __m128 qa1 = {qL->a1, qR->a1};
  __m128 qa2 = {qL->a2, qR->a2};

  __m128 z1 = {rL->y1, rR->y1};
  __m128 z2 = {rL->y2, rR->y2};
  __m128 rb0 = {rL->b0, rR->b0};
  __m128 rb1 = {rL->b1, rR->b1};
  __m128 rb2 = {rL->b2, rR->b2};
  __m128 ra1 = {rL->a1, rR->a1};
  __m128 ra2 = {rL->a2, rR->a2};

  // clang-format off
	__asm__ __volatile__(
		"1:                                     \n"
		"movss (%[data0]), %%xmm2               \n"
		"movss (%[data1]), %%xmm1               \n"
		"unpcklps %%xmm1, %%xmm2                \n"
		"mulps %[qb2],%[x2]                     \n"
		"lddqu %[qb0],%%xmm0                    \n"
		"mulps %[ra2],%[z2]                     \n"
		"lddqu %[qb1],%%xmm1                    \n"
		"mulps %%xmm2,%%xmm0                    \n"
		"mulps %[x1],%%xmm1                     \n"
		"addps %%xmm1,%%xmm0                    \n"
		"movaps %[qa1],%%xmm1                   \n"
		"mulps %[y1],%%xmm1                     \n"
		"addps %[x2],%%xmm0                     \n"
		"movaps %[rb1],%[x2]                    \n"
		"mulps %[y1],%[x2]                      \n"
		"subps %%xmm1,%%xmm0                    \n"
		"movaps %[qa2],%%xmm1                   \n"
		"mulps %[y2],%%xmm1                     \n"
		"mulps %[rb2],%[y2]                     \n"
		"subps %%xmm1,%%xmm0                    \n"
		"movaps %[rb0],%%xmm1                   \n"
		"mulps %%xmm0,%%xmm1                    \n"
		"addps %[x2],%%xmm1                     \n"
		"movaps %[x1],%[x2]                     \n"
		"movaps %%xmm2,%[x1]                    \n"
		"addps %[y2],%%xmm1                     \n"
		"movaps %[ra1],%[y2]                    \n"
		"mulps %[z1],%[y2]                      \n"
		"subps %[y2],%%xmm1                     \n"
		"movaps %[y1],%[y2]                     \n"
		"movaps %%xmm0,%[y1]                    \n"
		"subps %[z2],%%xmm1                     \n"
		"movaps %[z1],%[z2]                     \n"
		"movaps %%xmm1,%[z1]                    \n"
		"movss %%xmm1, (%[data0])               \n"
		"shufps $1, %%xmm1, %%xmm1              \n"
		"movss %%xmm1, (%[data1])               \n"
		"add $4, %[data0]                       \n"
		"add $4, %[data1]                       \n"
		"sub $1, %[count]                       \n"
		"jnz 1b                                 \n"
		: // output
		  [data0]"+r"(data0),
		  [data1]"+r"(data1),
		  [count]"+r"(count),
		  [x1]"+x"(x1),
		  [x2]"+x"(x2),
		  [y1]"+x"(y1),
		  [y2]"+x"(y2),
		  [z1]"+x"(z1),
		  [z2]"+x"(z2)
		: // input
		  [qb0]"m"(qb0),
		  [qb1]"m"(qb1),
		  [qb2]"m"(qb2),
		  [qa1]"x"(qa1),
		  [qa2]"x"(qa2),
		  [rb0]"x"(rb0),
		  [rb1]"x"(rb1),
		  [rb2]"x"(rb2),
		  [ra1]"x"(ra1),
		  [ra2]"x"(ra2)
		: // clobber
		  "xmm0", "xmm1", "xmm2", "memory", "cc");
  // clang-format on

  qL->x1 = x1[0];
  qL->x2 = x2[0];
  qL->y1 = y1[0];
  qL->y2 = y2[0];
  rL->y1 = z1[0];
  rL->y2 = z2[0];
  qR->x1 = x1[1];
  qR->x2 = x2[1];
  qR->y1 = y1[1];
  qR->y2 = y2[1];
  rR->y1 = z1[1];
  rR->y2 = z2[1];
}
#endif

void eq2_process(struct eq2* eq2, float* data0, float* data1, int count) {
  int i;
  int n;
  if (!count) {
    return;
  }
  n = eq2->n[0];
  if (eq2->n[1] > n) {
    n = eq2->n[1];
  }
  for (i = 0; i < n; i += EQ2_NUM_CHANNELS) {
    if (i + 1 == n) {
      eq2_process_one(&eq2->biquad[i], data0, data1, count);
    } else {
#if defined(__ARM_NEON__)
      eq2_process_two_neon(&eq2->biquad[i], data0, data1, count);
#elif defined(__SSE3__) && defined(__x86_64__)
      eq2_process_two_sse3(&eq2->biquad[i], data0, data1, count);
#else
      eq2_process_one(&eq2->biquad[i], data0, data1, count);
      eq2_process_one(&eq2->biquad[i + 1], data0, data1, count);
#endif
    }
  }
}

int eq2_convert_channel_response(struct eq2* eq2,
                                 int32_t* bq_cfg,
                                 int channel) {
  float accumulated_gain = 1.0;
  int ret;

  for (int i = 0; i < eq2->n[channel]; i++) {
    struct biquad* bq = &eq2->biquad[i][channel];

    /* For i = 0..(n-2), accumulated_gain is kept accumulating in loop.
     * For i = n-1, the last biquad element, accumulated_gain is dumped to the
     * converted blob by calling biquad_convert_blob() with dump_gain = 1.
     * To prevent the sample saturation on each node across the series of biquad
     * as the channel response intermediate nodes, considering that DSP EQ is
     * the fixed-point design.
     */
    ret = biquad_convert_blob(bq, bq_cfg, &accumulated_gain,
                              (i == eq2->n[channel] - 1) /* dump_gain */);
    if (ret < 0) {
      return ret;
    }
    bq_cfg += SOF_EQ_IIR_NBIQUAD;
  }
  return 0;
}

int eq2_convert_params_to_blob(struct eq2* eq2,
                               uint32_t** config,
                               size_t* config_size) {
  const size_t biquad_size = sizeof(struct sof_eq_iir_biquad);
  const size_t eq_iir_hdr_size = sizeof(struct sof_eq_iir_header);
  const size_t eq_cfg_hdr_size = sizeof(struct sof_eq_iir_config);

  if (!eq2) {
    return -ENOENT;
  }

  if (eq2->n[0] <= 0 || eq2->n[1] <= 0) {
    return -ENODATA;
  }

  size_t response_config_size[EQ2_NUM_CHANNELS] = {
      eq_iir_hdr_size + eq2->n[0] * biquad_size, /* response of ch-0 */
      eq_iir_hdr_size + eq2->n[1] * biquad_size  /* response of ch-1 */
  };

  size_t size =
      eq_cfg_hdr_size +                     /* sof_eq_iir_config header */
      EQ2_NUM_CHANNELS * sizeof(uint32_t) + /* assign_response[channels] */
      response_config_size[0] +             /* 1st response config data */
      response_config_size[1];              /* 2nd response config data */

  struct sof_eq_iir_config* eq_config =
      (struct sof_eq_iir_config*)calloc(1, size);
  if (!eq_config) {
    return -ENOMEM;
  }

  /* Fill sof_eq_iir_config header. */
  eq_config->size = size;
  eq_config->channels_in_config = EQ2_NUM_CHANNELS;
  eq_config->number_of_responses = EQ2_NUM_CHANNELS;

  /* Fill assign_response[channels]. */
  eq_config->data[0] = 0; /* assign response-0 to ch-0 */
  eq_config->data[1] = 1; /* assign response-1 to ch-1 */

  /* Fill config data per response. */
  struct sof_eq_iir_header* eq_hdr =
      (struct sof_eq_iir_header*)(&eq_config->data[2]);
  int ret;
  for (int channel = 0; channel < EQ2_NUM_CHANNELS; channel++) {
    /* Fill the header information. */
    eq_hdr->num_sections = eq2->n[channel];
    eq_hdr->num_sections_in_series = eq2->n[channel];

    /* Fill sof_eq_iir_biquad for biquads in one channel. */
    ret = eq2_convert_channel_response(eq2, eq_hdr->biquads, channel);
    if (ret < 0) {
      free(eq_config);
      return ret;
    }

    /* Move the address to the next sof_eq_iir_header element. */
    eq_hdr = (struct sof_eq_iir_header*)((uint8_t*)eq_hdr +
                                         response_config_size[channel]);
  }

  *config = (uint32_t*)eq_config;
  *config_size = size;
  return 0;
}
