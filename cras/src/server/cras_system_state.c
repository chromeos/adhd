/* Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <string.h>
#include <syslog.h>

#include "cras_alsa_card.h"
#include "cras_system_state.h"
#include "cras_util.h"
#include "utlist.h"

struct card_list {
	struct cras_alsa_card *card;
	struct card_list *prev, *next;
};

/* Holds a callback to notify when a setting such as volume or mute is changed.
 * When called, data will be passed back to the callback. */
struct volume_callback_list {
	cras_system_volume_changed_cb callback;
	void *data;
	struct volume_callback_list *prev, *next;
};

/* The system state.
 * Members:
 *    volume - index from 0-100.
 *    min_volume_dBFS - volume in dB * 100 when volume = 1.
 *    max_volume_dBFS - volume in dB * 100 when volume = max.
 *    mute - 0 = unmuted, 1 = muted.
 *    capture_gain - Capture gain in dBFS * 100.
 *    capture_mute - 0 = unmuted, 1 = muted.
 *    volume_callbacks - Called when the system volume changes.
 *    mute_callbacks - Called when the system mute state changes.
 *    capture_gain_callbacks - Called when the capture gain changes.
 *    capture_mute_callbacks - Called when the capture mute changes.
 *    volume_limits_callbacks - Called when the volume limits are changed.
 *    cards - A list of active sound cards in the system.
 */
static struct {
	size_t volume;
	long min_volume_dBFS;
	long max_volume_dBFS;
	int mute;
	long capture_gain;
	int capture_mute;
	struct volume_callback_list *volume_callbacks;
	struct volume_callback_list *mute_callbacks;
	struct volume_callback_list *capture_gain_callbacks;
	struct volume_callback_list *capture_mute_callbacks;
	struct volume_callback_list *volume_limits_callbacks;
	struct card_list *cards;
} state;

/* Adds the callback, cb, to the list.  arg will be passed to the callback when
 * it is called. */
static int register_callback(struct volume_callback_list **list,
			     cras_system_volume_changed_cb cb,
			     void *arg)
{
	struct volume_callback_list *vol_cb;

	if (cb == NULL)
		return -EINVAL;

	DL_FOREACH(*list, vol_cb)
		if (vol_cb->callback == cb && vol_cb->data == arg)
			return -EEXIST;

	vol_cb = calloc(1, sizeof(*vol_cb));
	if (vol_cb == NULL)
		return -ENOMEM;
	vol_cb->callback = cb;
	vol_cb->data = arg;
	DL_APPEND(*list, vol_cb);
	return 0;
}

/* Removes cb from list, iff both cb and arg match an entry. */
static int remove_callback(struct volume_callback_list **list,
			   cras_system_volume_changed_cb cb,
			   void *arg)
{
	struct volume_callback_list *vol_cb, *tmp;

	DL_FOREACH_SAFE(*list, vol_cb, tmp)
		if (vol_cb->callback == cb && vol_cb->data == arg) {
			DL_DELETE(*list, vol_cb);
			free(vol_cb);
			return 0;
		}
	return -ENOENT;
}

/*
 * Exported Interface.
 */

void cras_system_state_init()
{
	struct volume_callback_list *cb, *tmp;

	state.volume = CRAS_MAX_SYSTEM_VOLUME;
	state.mute = 0;
	state.capture_gain = 0;
	state.capture_mute = 0;

	/* Free any registered callbacks.  This prevents unit tests from
	 * leaking. */
	DL_FOREACH_SAFE(state.volume_callbacks, cb, tmp) {
		DL_DELETE(state.volume_callbacks, cb);
		free(cb);
	}
	state.volume_callbacks = NULL;

	DL_FOREACH_SAFE(state.mute_callbacks, cb, tmp) {
		DL_DELETE(state.mute_callbacks, cb);
		free(cb);
	}
	state.mute_callbacks = NULL;

	DL_FOREACH_SAFE(state.capture_gain_callbacks, cb, tmp) {
		DL_DELETE(state.capture_gain_callbacks, cb);
		free(cb);
	}
	state.capture_gain_callbacks = NULL;

	DL_FOREACH_SAFE(state.capture_mute_callbacks, cb, tmp) {
		DL_DELETE(state.capture_mute_callbacks, cb);
		free(cb);
	}
	state.capture_mute_callbacks = NULL;

	DL_FOREACH_SAFE(state.volume_limits_callbacks, cb, tmp) {
		DL_DELETE(state.volume_limits_callbacks, cb);
		free(cb);
	}
	state.volume_limits_callbacks = NULL;
}

void cras_system_set_volume(size_t volume)
{
	struct volume_callback_list *vol_cb;

	if (volume > CRAS_MAX_SYSTEM_VOLUME)
		syslog(LOG_DEBUG, "system volume set out of range %zu", volume);

	state.volume = min(volume, CRAS_MAX_SYSTEM_VOLUME);
	DL_FOREACH(state.volume_callbacks, vol_cb)
		vol_cb->callback(vol_cb->data);
}

size_t cras_system_get_volume()
{
	return state.volume;
}

int cras_system_register_volume_changed_cb(cras_system_volume_changed_cb cb,
					   void *arg)
{
	return register_callback(&state.volume_callbacks, cb, arg);
}

int cras_system_remove_volume_changed_cb(cras_system_volume_changed_cb cb,
					 void *arg)
{
	return remove_callback(&state.volume_callbacks, cb, arg);
}

void cras_system_set_capture_gain(long gain)
{
	struct volume_callback_list *capture_cb;

	state.capture_gain = gain;
	DL_FOREACH(state.capture_gain_callbacks, capture_cb)
		capture_cb->callback(capture_cb->data);
}

long cras_system_get_capture_gain()
{
	return state.capture_gain;
}

int cras_system_register_capture_gain_changed_cb(
		cras_system_volume_changed_cb cb,
		void *arg)
{
	return register_callback(&state.capture_gain_callbacks, cb, arg);
}

int cras_system_remove_capture_gain_changed_cb(cras_system_volume_changed_cb cb,
					       void *arg)
{
	return remove_callback(&state.capture_gain_callbacks, cb, arg);
}

void cras_system_set_mute(int mute)
{
	struct volume_callback_list *mute_cb;

	state.mute = !!mute;
	DL_FOREACH(state.mute_callbacks, mute_cb)
		mute_cb->callback(mute_cb->data);
}

int cras_system_get_mute()
{
	return state.mute;
}

int cras_system_register_mute_changed_cb(cras_system_volume_changed_cb cb,
					 void *arg)
{
	return register_callback(&state.mute_callbacks, cb, arg);
}

int cras_system_remove_mute_changed_cb(cras_system_volume_changed_cb cb,
				       void *arg)
{
	return remove_callback(&state.mute_callbacks, cb, arg);
}

void cras_system_set_capture_mute(int mute)
{
	struct volume_callback_list *mute_cb;

	state.capture_mute = !!mute;
	DL_FOREACH(state.capture_mute_callbacks, mute_cb)
		mute_cb->callback(mute_cb->data);
}

int cras_system_get_capture_mute()
{
	return state.capture_mute;
}

int cras_system_register_capture_mute_changed_cb(
		cras_system_volume_changed_cb cb, void *arg)
{
	return register_callback(&state.capture_mute_callbacks, cb, arg);
}

int cras_system_remove_capture_mute_changed_cb(
		cras_system_volume_changed_cb cb, void *arg)
{
	return remove_callback(&state.capture_mute_callbacks, cb, arg);
}

void cras_system_set_volume_limits(long min, long max)
{
	struct volume_callback_list *limit_cb;

	state.min_volume_dBFS = min;
	state.max_volume_dBFS = max;
	DL_FOREACH(state.volume_limits_callbacks, limit_cb)
		limit_cb->callback(limit_cb->data);
}

long cras_system_get_min_volume()
{
	return state.min_volume_dBFS;
}

long cras_system_get_max_volume()
{
	return state.max_volume_dBFS;
}

int cras_system_register_volume_limits_changed_cb(
		cras_system_volume_changed_cb cb, void *arg)
{
	return register_callback(&state.volume_limits_callbacks, cb, arg);
}

int cras_system_remove_volume_limits_changed_cb(
		cras_system_volume_changed_cb cb, void *arg)
{
	return remove_callback(&state.volume_limits_callbacks, cb, arg);
}

int cras_system_add_alsa_card(size_t alsa_card_index)
{
	struct card_list *card;
	struct cras_alsa_card *alsa_card;

	DL_FOREACH(state.cards, card) {
		if (alsa_card_index == cras_alsa_card_get_index(card->card))
			return -EINVAL;
	}
	alsa_card = cras_alsa_card_create(alsa_card_index);
	if (alsa_card == NULL)
		return -ENOMEM;
	card = calloc(1, sizeof(*card));
	if (card == NULL)
		return -ENOMEM;
	card->card = alsa_card;
	DL_APPEND(state.cards, card);
	return 0;
}

int cras_system_remove_alsa_card(size_t alsa_card_index)
{
	struct card_list *card;

	DL_FOREACH(state.cards, card) {
		if (alsa_card_index == cras_alsa_card_get_index(card->card))
			break;
	}
	if (card == NULL)
		return -EINVAL;
	DL_DELETE(state.cards, card);
	cras_alsa_card_destroy(card->card);
	free(card);
	return 0;
}
