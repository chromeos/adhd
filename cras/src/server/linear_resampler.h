/* Copyright (c) 2014 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef LINEAR_RESAMPLER_H_
#define LINEAR_RESAMPLER_H_


struct linear_resampler;

/* Creates a linear resampler.
 * Args:
 *    num_channels - The number of channels in each frames.
 *    format_bytes - The length of one frame in bytes.
 *    src_rate - The source rate to resample from.
 *    dst_rate - The destination rate to resample to.
 */
struct linear_resampler *linear_resampler_create(unsigned int num_channels,
					     unsigned int format_bytes,
					     unsigned int src_rate,
					     unsigned int dst_rate);

/* Sets the rates for the linear resampler.
 * Args:
 *    from - The rate to resample from.
 *    to - The rate to resample to.
 */
void linear_resampler_set_rates(struct linear_resampler *lr,
			      unsigned int from,
			      unsigned int to);


/* Run linear resample for audio samples.
 * Args:
 *    lr - The linear resampler.
 *    src - The input buffer.
 *    src_frames - The number of frames of input buffer.
 *    dst - The output buffer.
 *    dst_frames - The number of frames of output buffer.
 */
unsigned int linear_resampler_resample(struct linear_resampler *lr,
			     uint8_t *src,
			     unsigned int *src_frames,
			     uint8_t *dst,
			     unsigned dst_frames);

/* Destroy a linear resampler. */
void linear_resampler_destroy(struct linear_resampler *lr);

#endif /* LINEAR_RESAMPLER_H_ */
