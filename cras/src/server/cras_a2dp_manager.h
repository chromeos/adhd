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
					 const char *name, int sample_rate,
					 int bits_per_sample, int channel_mode);

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
 * Returns:
 *    0 for success, otherwise error code.
 */
int cras_floss_a2dp_start(struct cras_a2dp *a2dp,
			  struct cras_audio_format *fmt);

/* Stops a2dp streaming. */
int cras_floss_a2dp_stop(struct cras_a2dp *a2dp);

/* Gets the file descriptor to write to the given cras_a2dp.
 * Returns -1 if given cras_a2dp isn't started. */
int cras_floss_a2dp_get_fd(struct cras_a2dp *a2dp);

/* Schedules repeated delay sync tasks at time init_msec + N * period_msec
 * where N = 0, 1, 2...
 * Args:
 *    init_msec - We don't know when BT delay is available so just guess a time
 *        and schedule the first delay sync.
 *    period_msec - The time interval between repeated delay sync tasks.
 */
void cras_floss_a2dp_delay_sync(struct cras_a2dp *a2dp, unsigned int init_msec,
				unsigned int period_msec);

/* Fills the format property lists by Floss defined format bitmaps. */
int cras_floss_a2dp_fill_format(int sample_rate, int bits_per_sample,
				int channel_mode, size_t **rates,
				snd_pcm_format_t **formats,
				size_t **channel_counts);

/* Sets if the a2dp audio device supports absolute volume. */
void cras_floss_a2dp_set_support_absolute_volume(struct cras_a2dp *a2dp,
						 bool support_absolute_volume);

/* Gets if the a2dp audio device supports absolute volume. */
bool cras_floss_a2dp_get_support_absolute_volume(struct cras_a2dp *a2dp);

/* Update the volume when receiving absolute volume changed event from the
 * headset. */
void cras_floss_a2dp_update_volume(struct cras_a2dp *a2dp, unsigned int volume);

/* Set the volume of connected a2dp device. */
void cras_floss_a2dp_set_volume(struct cras_a2dp *a2dp, unsigned int volume);

/* Schedule a suspend request of the a2dp device. Should be called in
 * the context of audio threead. */
void cras_a2dp_schedule_suspend(unsigned int msec);

/* Cancel a pending suspend request if exist of the a2dp device. */
void cras_a2dp_cancel_suspend();

#endif /* CRAS_A2DP_MANAGER_H_ */
