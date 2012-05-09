/* Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <alsa/asoundlib.h>
#include <stddef.h>
#include <stdlib.h>

#include "cras_types.h"

/* Create an audio format structure. */
struct cras_audio_format *cras_audio_format_create(snd_pcm_format_t format,
						   size_t frame_rate,
						   size_t num_channels)
{
	struct cras_audio_format *fmt;

	fmt = (struct cras_audio_format *)calloc(1, sizeof(*fmt));
	if (fmt == NULL)
		return fmt;

	fmt->format = format;
	fmt->frame_rate = frame_rate;
	fmt->num_channels = num_channels;
	return fmt;
}

/* Destroy an audio format struct created with cras_audio_format_crate. */
void cras_audio_format_destroy(struct cras_audio_format *fmt)
{
	free(fmt);
}
