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

static struct {
	size_t volume; /* Volume index from 0-100. */
	int mute; /* 0 = unmuted, 1 = muted. */
	cras_system_volume_changed_cb volume_callback;
	void *volume_callback_data;
	cras_system_mute_changed_cb mute_callback;
	void *mute_callback_data;
	struct card_list *cards;
} state;

void cras_system_state_init()
{
	state.volume = CRAS_MAX_SYSTEM_VOLUME;
	state.mute = 0;
	state.volume_callback = NULL;
	state.volume_callback_data = NULL;
	state.mute_callback = NULL;
	state.mute_callback_data = NULL;
}

void cras_system_set_volume(size_t volume)
{
	if (volume > CRAS_MAX_SYSTEM_VOLUME)
		syslog(LOG_DEBUG, "system volume set out of range %zu", volume);

	state.volume = min(volume, CRAS_MAX_SYSTEM_VOLUME);
	if (state.volume_callback != NULL)
		state.volume_callback(state.volume, state.volume_callback_data);
}

size_t cras_system_get_volume()
{
	return state.volume;
}

void cras_system_register_volume_changed_cb(cras_system_volume_changed_cb cb,
					    void *arg)
{
	state.volume_callback = cb;
	state.volume_callback_data = arg;
}

void cras_system_set_mute(int mute)
{
	state.mute = !!mute;
	if (state.mute_callback != NULL)
		state.mute_callback(state.mute, state.mute_callback_data);
}

int cras_system_get_mute()
{
	return state.mute;
}

void cras_system_register_mute_changed_cb(cras_system_mute_changed_cb cb,
					  void *arg)
{
	state.mute_callback = cb;
	state.mute_callback_data = arg;
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
