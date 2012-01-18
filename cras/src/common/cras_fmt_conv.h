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

/* Get the buffer to store the pre-converted samples in */
uint8_t *cras_fmt_conv_get_buffer(struct cras_fmt_conv *conv);
/* Get the number of output frames that will result from converting in_frames */
size_t cras_fmt_conv_in_frames_to_out(struct cras_fmt_conv *conv,
				      size_t in_frames);
/* Get the number of input frames that will result from converting out_frames */
size_t cras_fmt_conv_out_frames_to_in(struct cras_fmt_conv *conv,
				      size_t out_frames);
/* Convert the currently filled buffer and put results in out_buf.
 * Return number of frames put in out_buf. */
size_t cras_fmt_conv_convert_to(struct cras_fmt_conv *conv, uint8_t *out_buf,
				size_t in_frames);

#endif /* CRAS_FMT_CONV_H_ */
