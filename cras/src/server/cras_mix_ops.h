/* Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef CRAS_MIX_OPS_H_
#define CRAS_MIX_OPS_H_

#include <stdint.h>

#include "cras_system_state.h"

extern const struct cras_mix_ops mixer_ops;
extern const struct cras_mix_ops mixer_ops_sse42;
extern const struct cras_mix_ops mixer_ops_avx;
extern const struct cras_mix_ops mixer_ops_avx2;
extern const struct cras_mix_ops mixer_ops_fma;

struct cras_mix_ops {
	void (*scale_buffer)(snd_pcm_format_t fmt, uint8_t *buff,
			unsigned int count, float scaler);
	void (*add)(snd_pcm_format_t fmt, uint8_t *dst, uint8_t *src,
		  unsigned int count, unsigned int index,
		  int mute, float mix_vol);
	void (*add_scale_stride)(snd_pcm_format_t fmt, uint8_t *dst,
			uint8_t *src, unsigned int count,
			unsigned int dst_stride, unsigned int src_stride,
			float scaler);
	size_t (*mute_buffer)(uint8_t *dst,
			    size_t frame_bytes,
			    size_t count);
};
#endif
