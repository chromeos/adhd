/* Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/*
 * Used to convert from one audio format to another.  Currently only supports
 * sample rate conversion with the speex backend.
 */
#ifndef CRAS_FMT_CONV_H_
#define CRAS_FMT_CONV_H_

#include <stdint.h>
#include <stdlib.h>

struct cras_audio_format;
struct cras_fmt_conv;

/* Create and destroy format converters. */
struct cras_fmt_conv *cras_fmt_conv_create(const struct cras_audio_format *in,
					   const struct cras_audio_format *out,
					   size_t max_frames);
void cras_fmt_conv_destroy(struct cras_fmt_conv *conv);

/* Get the number of output frames that will result from converting in_frames */
size_t cras_fmt_conv_in_frames_to_out(struct cras_fmt_conv *conv,
				      size_t in_frames);
/* Get the number of input frames that will result from converting out_frames */
size_t cras_fmt_conv_out_frames_to_in(struct cras_fmt_conv *conv,
				      size_t out_frames);
/* Converts in_frames samples from in_buf, storing the results in out_buf.
 * Args:
 *    conv - The format converter returned from cras_fmt_conv_create().
 *    in_buf - Samples to convert.
 *    out_buf - Converted samples are placed here.
 *    in_frames - Number of frames from in_buf to convert.
 *    out_frames - Maximum number of frames to store in out_buf.  If there isn't
 *      any format conversion, out_frames must be >= in_frames.  When doing
 *      format conversion out_frames should be able to hold all the converted
 *      frames, this can be checked with cras_fmt_conv_in_frames_to_out().
 * Return number of frames put in out_buf. */
size_t cras_fmt_conv_convert_frames(struct cras_fmt_conv *conv,
				    uint8_t *in_buf,
				    uint8_t *out_buf,
				    size_t in_frames,
				    size_t out_frames);

#endif /* CRAS_FMT_CONV_H_ */
