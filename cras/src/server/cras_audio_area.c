/* Copyright (c) 2014 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <stdlib.h>

#include "cras_audio_area.h"
#include "cras_audio_format.h"

struct cras_audio_area *cras_audio_area_create(int num_channels)
{
	struct cras_audio_area *area;
	size_t sz;

	sz = sizeof(*area) + num_channels * sizeof(struct cras_channel_area);
	area = calloc(1, sz);
	area->num_channels = num_channels;

	return area;
}

void cras_audio_area_destroy(struct cras_audio_area *area)
{
	free(area);
}

void cras_audio_area_config_channels(struct cras_audio_area *area,
				     const struct cras_audio_format *fmt)
{
	unsigned int i, ch;

	for (i = 0; i < fmt->num_channels; i++) {
		area->channels[i].ch_set = 0;
		for (ch = 0; ch < CRAS_CH_MAX; ch++)
			if (fmt->channel_layout[ch] == i)
				channel_area_set_channel(&area->channels[i], ch);
	}

	/* For mono, config the channel type to match both front
	 * left and front right.
	 * TODO(hychao): add more mapping when we have like {FL, FC}
	 * for mono + kb mic.
	 */
	if ((fmt->num_channels == 1) &&
	    (fmt->channel_layout[CRAS_CH_FL] == 0))
		channel_area_set_channel(area->channels, CRAS_CH_FR);
}

void cras_audio_area_config_buf_pointers(struct cras_audio_area *area,
					 const struct cras_audio_format *fmt,
					 uint8_t *base_buffer)
{
	int i;
	const int sample_size = snd_pcm_format_physical_width(fmt->format) / 8;

	/* TODO(dgreid) - assuming interleaved audio here for now. */
	for (i = 0 ; i < area->num_channels; i++) {
		area->channels[i].step_bytes = cras_get_format_bytes(fmt);
		area->channels[i].buf = base_buffer + i * sample_size;
	}
}
