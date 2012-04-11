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
 *    mute - 0 = unmuted, 1 = muted.
 *    volume_callbacks - Called when the system volume changes.
 *    mute_callbacks - Called when the system mute state changes.
 *    cards - A list of active sound cards in the system.
 */
static struct {
	size_t volume;
	int mute;
	struct volume_callback_list *volume_callbacks;
	struct volume_callback_list *mute_callbacks;
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
