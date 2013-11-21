/* Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <alsa/asoundlib.h>
#include <stddef.h>
#include <stdlib.h>

#include "cras_audio_format.h"

/* Create an audio format structure. */
struct cras_audio_format *cras_audio_format_create(snd_pcm_format_t format,
						   size_t frame_rate,
						   size_t num_channels)
{
	struct cras_audio_format *fmt;
	int i;

	fmt = (struct cras_audio_format *)calloc(1, sizeof(*fmt));
	if (fmt == NULL)
		return fmt;

	fmt->format = format;
	fmt->frame_rate = frame_rate;
	fmt->num_channels = num_channels;

	/* Initialize all channel position to -1(not set) */
	for (i = 0; i < CRAS_CH_MAX; i++)
		fmt->channel_layout[i] = -1;

	return fmt;
}

int cras_audio_format_set_channel_layout(struct cras_audio_format *format,
					 int8_t layout[CRAS_CH_MAX])
{
	int i;

	/* Check that the max channel index should not exceed the
	 * channel count set in format.
	 */
	for (i = 0; i < CRAS_CH_MAX; i++)
		if (layout[i] >= (int)format->num_channels)
			return -EINVAL;

	for (i = 0; i < CRAS_CH_MAX; i++)
		format->channel_layout[i] = layout[i];

	return 0;
}

/* Destroy an audio format struct created with cras_audio_format_crate. */
void cras_audio_format_destroy(struct cras_audio_format *fmt)
{
	free(fmt);
}
