/* Copyright 2021 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef CRAS_A2DP_MANAGER_H_
#define CRAS_A2DP_MANAGER_H_

#include "cras_audio_format.h"

struct cras_a2dp;
struct fl_media;

/* Creates cras_a2dp object representing a connected a2dp device. */
struct cras_a2dp *cras_floss_a2dp_create(struct fl_media *fm, const char *addr,
					 int sample_rate, int bits_per_sample,
					 int channel_mode);

/* Destroys given cras_a2dp object. */
void cras_floss_a2dp_destroy(struct cras_a2dp *a2dp);

/* Gets the human readable name of a2dp device. */
const char *cras_floss_a2dp_get_display_name(struct cras_a2dp *a2dp);

/* Gets the address of connected a2dp device. */
const char *cras_floss_a2dp_get_addr(struct cras_a2dp *a2dp);

/* Starts a2dp streaming.
 * Args:
 *    a2dp - The a2dp instance to start streaming.
 *    fmt - The PCM format to select for streaming.
 *    skt - To be filled with the socket to write PCM audio to. The
 *        caller is responsible to close this socket when done with it.
 * Returns:
 *    0 for success, otherwise error code.
 */
int cras_floss_a2dp_start(struct cras_a2dp *a2dp, struct cras_audio_format *fmt,
			  int *skt);

/* Stops a2dp streaming. */
int cras_floss_a2dp_stop(struct cras_a2dp *a2dp);

/* Fills the format property lists by Floss defined format bitmaps. */
int cras_floss_a2dp_fill_format(int sample_rate, int bits_per_sample,
				int channel_mode, size_t **rates,
				snd_pcm_format_t **formats,
				size_t **channel_counts);

/* Schedule a suspend request of the a2dp device. Should be called in
 * the context of audio threead. */
void cras_a2dp_schedule_suspend(unsigned int msec);

/* Cancel a pending suspend request if exist of the a2dp device. */
void cras_a2dp_cancel_suspend();

#endif /* CRAS_A2DP_MANAGER_H_ */
