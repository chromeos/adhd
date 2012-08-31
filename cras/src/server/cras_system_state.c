/* Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <string.h>
#include <stdlib.h>
#include <sys/shm.h>
#include <sys/stat.h>
#include <syslog.h>

#include "cras_alsa_card.h"
#include "cras_config.h"
#include "cras_device_blacklist.h"
#include "cras_system_state.h"
#include "cras_types.h"
#include "cras_util.h"
#include "utlist.h"

struct card_list {
	struct cras_alsa_card *card;
	struct card_list *prev, *next;
};

/* Holds a callback to notify when a setting such as volume or mute is changed.
 * When called, data will be passed back to the callback. */
struct state_callback_list {
	cras_system_state_changed_cb callback;
	void *data;
	struct state_callback_list *prev, *next;
};

/* The system state.
 * Members:
 *    exp_state - The exported system state shared with clients.
 *    shm_key - Key for shm area of system_state struct.
 *    shm_id - Id for shm area of system_state struct.
 *    device_blacklist - Blacklist of device the server will ignore.
 *    volume_callbacks - Called when the system volume changes.
 *    mute_callbacks - Called when the system mute state changes.
 *    capture_gain_callbacks - Called when the capture gain changes.
 *    capture_mute_callbacks - Called when the capture mute changes.
 *    volume_limits_callbacks - Called when the volume limits are changed.
 *    cards - A list of active sound cards in the system.
 */
static struct {
	struct cras_server_state *exp_state;
	key_t shm_key;
	int shm_id;
	struct cras_device_blacklist *device_blacklist;
	struct state_callback_list *volume_callbacks;
	struct state_callback_list *mute_callbacks;
	struct state_callback_list *capture_gain_callbacks;
	struct state_callback_list *capture_mute_callbacks;
	struct state_callback_list *volume_limits_callbacks;
	struct card_list *cards;
	/* Select loop callback registration. */
	int (*fd_add)(int fd, void (*cb)(void *data),
		      void *cb_data, void *select_data);
	void (*fd_rm)(int fd, void *select_data);
	void *select_data;
} state;

/* Adds the callback, cb, to the list.  arg will be passed to the callback when
 * it is called. */
static int register_callback(struct state_callback_list **list,
			     cras_system_state_changed_cb cb,
			     void *arg)
{
	struct state_callback_list *state_cb;

	if (cb == NULL)
		return -EINVAL;

	DL_FOREACH(*list, state_cb)
		if (state_cb->callback == cb && state_cb->data == arg)
			return -EEXIST;

	state_cb = calloc(1, sizeof(*state_cb));
	if (state_cb == NULL)
		return -ENOMEM;
	state_cb->callback = cb;
	state_cb->data = arg;
	DL_APPEND(*list, state_cb);
	return 0;
}

/* Removes cb from list, iff both cb and arg match an entry. */
static int remove_callback(struct state_callback_list **list,
			   cras_system_state_changed_cb cb,
			   void *arg)
{
	struct state_callback_list *state_cb, *tmp;

	DL_FOREACH_SAFE(*list, state_cb, tmp)
		if (state_cb->callback == cb && state_cb->data == arg) {
			DL_DELETE(*list, state_cb);
			free(state_cb);
			return 0;
		}
	return -ENOENT;
}

/*
 * Exported Interface.
 */

void cras_system_state_init()
{
	struct cras_server_state *exp_state;
	unsigned loops = 0;

	/* Find an available shm key. */
	do {
		state.shm_key = getpid() + rand();
		state.shm_id = shmget(state.shm_key, sizeof(*exp_state),
				      IPC_CREAT | IPC_EXCL | 0640);
	} while (state.shm_id < 0 && loops++ < 100);
	if (state.shm_id < 0) {
		syslog(LOG_ERR, "Fatal: system state can't shmget");
		exit(state.shm_id);
	}

	exp_state = shmat(state.shm_id, NULL, 0);
	if (exp_state == (void *)-1) {
		syslog(LOG_ERR, "Fatal: system state can't shmat");
		exit(-ENOMEM);
	}

	/* Initial system state. */
	exp_state->state_version = CRAS_SERVER_STATE_VERSION;
	exp_state->volume = CRAS_MAX_SYSTEM_VOLUME;
	exp_state->mute = 0;
	exp_state->mute_locked = 0;
	exp_state->capture_gain = DEFAULT_CAPTURE_GAIN;
	exp_state->capture_mute = 0;
	exp_state->capture_mute_locked = 0;
	exp_state->min_volume_dBFS = DEFAULT_MIN_VOLUME_DBFS;
	exp_state->max_volume_dBFS = DEFAULT_MAX_VOLUME_DBFS;
	exp_state->min_capture_gain = DEFAULT_MIN_CAPTURE_GAIN;
	exp_state->max_capture_gain = DEFAULT_MAX_CAPTURE_GAIN;
	exp_state->num_streams_attached = 0;

	state.exp_state = exp_state;

	/* Read config file for blacklisted devices. */
	state.device_blacklist =
		cras_device_blacklist_create(CRAS_CONFIG_FILE_DIR);
}

void cras_system_state_deinit()
{
	struct state_callback_list *cb, *tmp;

	/* Free any resources used.  This prevents unit tests from leaking. */

	cras_device_blacklist_destroy(state.device_blacklist);

	if (state.exp_state) {
		shmdt(state.exp_state);
		shmctl(state.shm_id, IPC_RMID, (void *)state.exp_state);
	}

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
	struct state_callback_list *vol_cb;

	if (volume > CRAS_MAX_SYSTEM_VOLUME)
		syslog(LOG_DEBUG, "system volume set out of range %zu", volume);

	state.exp_state->volume = min(volume, CRAS_MAX_SYSTEM_VOLUME);
	DL_FOREACH(state.volume_callbacks, vol_cb)
		vol_cb->callback(vol_cb->data);
}

size_t cras_system_get_volume()
{
	return state.exp_state->volume;
}

int cras_system_register_volume_changed_cb(cras_system_state_changed_cb cb,
					   void *arg)
{
	return register_callback(&state.volume_callbacks, cb, arg);
}

int cras_system_remove_volume_changed_cb(cras_system_state_changed_cb cb,
					 void *arg)
{
	return remove_callback(&state.volume_callbacks, cb, arg);
}

void cras_system_set_capture_gain(long gain)
{
	struct state_callback_list *capture_cb;

	state.exp_state->capture_gain = gain;
	DL_FOREACH(state.capture_gain_callbacks, capture_cb)
		capture_cb->callback(capture_cb->data);
}

long cras_system_get_capture_gain()
{
	return state.exp_state->capture_gain;
}

int cras_system_register_capture_gain_changed_cb(
		cras_system_state_changed_cb cb,
		void *arg)
{
	return register_callback(&state.capture_gain_callbacks, cb, arg);
}

int cras_system_remove_capture_gain_changed_cb(cras_system_state_changed_cb cb,
					       void *arg)
{
	return remove_callback(&state.capture_gain_callbacks, cb, arg);
}

void cras_system_set_mute(int mute)
{
	struct state_callback_list *mute_cb;

	if (state.exp_state->mute_locked)
		return;

	state.exp_state->mute = !!mute;
	DL_FOREACH(state.mute_callbacks, mute_cb)
		mute_cb->callback(mute_cb->data);
}

void cras_system_set_mute_locked(int locked)
{
	struct state_callback_list *mute_cb;

	state.exp_state->mute_locked = !!locked;

	if (!state.exp_state->mute_locked) {
		DL_FOREACH(state.mute_callbacks, mute_cb)
			mute_cb->callback(mute_cb->data);
	}
}

int cras_system_get_mute()
{
	return state.exp_state->mute;
}

int cras_system_get_mute_locked()
{
	return state.exp_state->mute_locked;
}

int cras_system_register_mute_changed_cb(cras_system_state_changed_cb cb,
					 void *arg)
{
	return register_callback(&state.mute_callbacks, cb, arg);
}

int cras_system_remove_mute_changed_cb(cras_system_state_changed_cb cb,
				       void *arg)
{
	return remove_callback(&state.mute_callbacks, cb, arg);
}

void cras_system_set_capture_mute(int mute)
{
	struct state_callback_list *mute_cb;

	if (state.exp_state->capture_mute_locked)
		return;

	state.exp_state->capture_mute = !!mute;
	DL_FOREACH(state.capture_mute_callbacks, mute_cb)
		mute_cb->callback(mute_cb->data);
}

void cras_system_set_capture_mute_locked(int locked)
{
	struct state_callback_list *mute_cb;

	state.exp_state->capture_mute_locked = !!locked;

	if (!state.exp_state->capture_mute_locked) {
		DL_FOREACH(state.capture_mute_callbacks, mute_cb)
			mute_cb->callback(mute_cb->data);
	}
}

int cras_system_get_capture_mute()
{
	return state.exp_state->capture_mute;
}

int cras_system_get_capture_mute_locked()
{
	return state.exp_state->capture_mute_locked;
}

int cras_system_register_capture_mute_changed_cb(
		cras_system_state_changed_cb cb, void *arg)
{
	return register_callback(&state.capture_mute_callbacks, cb, arg);
}

int cras_system_remove_capture_mute_changed_cb(
		cras_system_state_changed_cb cb, void *arg)
{
	return remove_callback(&state.capture_mute_callbacks, cb, arg);
}

void cras_system_set_volume_limits(long min, long max)
{
	struct state_callback_list *limit_cb;

	state.exp_state->min_volume_dBFS = min;
	state.exp_state->max_volume_dBFS = max;
	DL_FOREACH(state.volume_limits_callbacks, limit_cb)
		limit_cb->callback(limit_cb->data);
}

long cras_system_get_min_volume()
{
	return state.exp_state->min_volume_dBFS;
}

long cras_system_get_max_volume()
{
	return state.exp_state->max_volume_dBFS;
}

int cras_system_register_volume_limits_changed_cb(
		cras_system_state_changed_cb cb, void *arg)
{
	return register_callback(&state.volume_limits_callbacks, cb, arg);
}

int cras_system_remove_volume_limits_changed_cb(
		cras_system_state_changed_cb cb, void *arg)
{
	return remove_callback(&state.volume_limits_callbacks, cb, arg);
}

void cras_system_set_capture_gain_limits(long min, long max)
{
	struct state_callback_list *limit_cb;

	state.exp_state->min_capture_gain = min;
	state.exp_state->max_capture_gain = max;
	DL_FOREACH(state.volume_limits_callbacks, limit_cb)
		limit_cb->callback(limit_cb->data);
}

long cras_system_get_min_capture_gain()
{
	return state.exp_state->min_capture_gain;
}

long cras_system_get_max_capture_gain()
{
	return state.exp_state->max_capture_gain;
}

int cras_system_has_played_streams()
{
	return state.exp_state->num_streams_attached != 0;
}

unsigned int cras_system_increment_streams_played()
{
	return ++state.exp_state->num_streams_attached;
}

int cras_system_add_alsa_card(struct cras_alsa_card_info *alsa_card_info)
{
	struct card_list *card;
	struct cras_alsa_card *alsa_card;
	unsigned card_index;

	if (alsa_card_info == NULL)
		return -EINVAL;

	card_index = alsa_card_info->card_index;

	DL_FOREACH(state.cards, card) {
		if (card_index == cras_alsa_card_get_index(card->card))
			return -EINVAL;
	}
	alsa_card = cras_alsa_card_create(alsa_card_info,
					  state.device_blacklist);
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

int cras_system_alsa_card_exists(unsigned alsa_card_index)
{
	struct card_list *card;

	DL_FOREACH(state.cards, card)
		if (alsa_card_index == cras_alsa_card_get_index(card->card))
			return 1;
	return 0;
}

int cras_system_set_select_handler(int (*add)(int fd,
					      void (*callback)(void *data),
					      void *callback_data,
					      void *select_data),
				   void (*rm)(int fd, void *select_data),
				   void *select_data)
{
	if (state.fd_add != NULL || state.fd_rm != NULL)
		return -EEXIST;
	state.fd_add = add;
	state.fd_rm = rm;
	state.select_data = select_data;
	return 0;
}

int cras_system_add_select_fd(int fd,
			      void (*callback)(void *data),
			      void *callback_data)
{
	if (state.fd_add == NULL)
		return -EINVAL;
	return state.fd_add(fd, callback, callback_data,
			    state.select_data);
}

void cras_system_rm_select_fd(int fd)
{
	if (state.fd_rm != NULL)
		state.fd_rm(fd, state.select_data);
}

struct cras_server_state *cras_system_state_update_begin()
{
	__sync_fetch_and_add(&state.exp_state->update_count, 1);
	return state.exp_state;
}

void cras_system_state_update_complete()
{
	__sync_fetch_and_add(&state.exp_state->update_count, 1);
}

key_t cras_sys_state_shm_key()
{
	return state.shm_key;
}
