/* Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <pthread.h>
#include <string.h>
#include <stdlib.h>
#include <sys/shm.h>
#include <sys/stat.h>
#include <syslog.h>

#include "cras_alsa_card.h"
#include "cras_config.h"
#include "cras_device_blacklist.h"
#include "cras_system_state.h"
#include "cras_tm.h"
#include "cras_types.h"
#include "cras_util.h"
#include "utlist.h"

struct card_list {
	struct cras_alsa_card *card;
	struct card_list *prev, *next;
};

/* The system state.
 * Members:
 *    exp_state - The exported system state shared with clients.
 *    shm_key - Key for shm area of system_state struct.
 *    shm_id - Id for shm area of system_state struct.
 *    device_blacklist - Blacklist of device the server will ignore.
 *    volume_alert - Called when the system volume changes.
 *    mute_alert - Called when the system mute state changes.
 *    capture_gain_alert - Called when the capture gain changes.
 *    capture_mute_alert - Called when the capture mute changes.
 *    volume_limits_alert - Called when the volume limits are changed.
 *    active_streams_alert - Called when the number of active streams changes.
 *    cards - A list of active sound cards in the system.
 *    update_lock - Protects the update_count, as audio threads can update the
 *      stream count.
 *    tm - The system-wide timer manager.
 */
static struct {
	struct cras_server_state *exp_state;
	key_t shm_key;
	int shm_id;
	struct cras_device_blacklist *device_blacklist;
	struct cras_alert *volume_alert;
	struct cras_alert *mute_alert;
	struct cras_alert *capture_gain_alert;
	struct cras_alert *capture_mute_alert;
	struct cras_alert *volume_limits_alert;
	struct cras_alert *active_streams_alert;
	struct card_list *cards;
	pthread_mutex_t update_lock;
	struct cras_tm *tm;
	/* Select loop callback registration. */
	int (*fd_add)(int fd, void (*cb)(void *data),
		      void *cb_data, void *select_data);
	void (*fd_rm)(int fd, void *select_data);
	void *select_data;
} state;

/*
 * Exported Interface.
 */

void cras_system_state_init()
{
	struct cras_server_state *exp_state;
	unsigned loops = 0;
	int rc;

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

	if ((rc = pthread_mutex_init(&state.update_lock, 0) != 0)) {
		syslog(LOG_ERR, "Fatal: system state mutex init");
		exit(rc);
	}

	state.exp_state = exp_state;

	/* Initialize alerts. */
	state.volume_alert = cras_alert_create(NULL);
	state.mute_alert = cras_alert_create(NULL);
	state.capture_gain_alert = cras_alert_create(NULL);
	state.capture_mute_alert = cras_alert_create(NULL);
	state.volume_limits_alert = cras_alert_create(NULL);
	state.active_streams_alert = cras_alert_create(NULL);

	state.tm = cras_tm_init();
	if (!state.tm) {
		syslog(LOG_ERR, "Fatal: system state timer init");
		exit(-ENOMEM);
	}

	/* Read config file for blacklisted devices. */
	state.device_blacklist =
		cras_device_blacklist_create(CRAS_CONFIG_FILE_DIR);
}

void cras_system_state_deinit()
{
	/* Free any resources used.  This prevents unit tests from leaking. */

	cras_device_blacklist_destroy(state.device_blacklist);

	cras_tm_deinit(state.tm);

	if (state.exp_state) {
		shmdt(state.exp_state);
		shmctl(state.shm_id, IPC_RMID, (void *)state.exp_state);
	}

	cras_alert_destroy(state.volume_alert);
	cras_alert_destroy(state.mute_alert);
	cras_alert_destroy(state.capture_gain_alert);
	cras_alert_destroy(state.capture_mute_alert);
	cras_alert_destroy(state.volume_limits_alert);
	cras_alert_destroy(state.active_streams_alert);

	state.volume_alert = NULL;
	state.mute_alert = NULL;
	state.capture_gain_alert = NULL;
	state.capture_mute_alert = NULL;
	state.volume_limits_alert = NULL;
	state.active_streams_alert = NULL;

	pthread_mutex_destroy(&state.update_lock);
}

void cras_system_set_volume(size_t volume)
{
	if (volume > CRAS_MAX_SYSTEM_VOLUME)
		syslog(LOG_DEBUG, "system volume set out of range %zu", volume);

	state.exp_state->volume = min(volume, CRAS_MAX_SYSTEM_VOLUME);
	cras_alert_pending(state.volume_alert);
}

size_t cras_system_get_volume()
{
	return state.exp_state->volume;
}

int cras_system_register_volume_changed_cb(cras_alert_cb cb, void *arg)
{
	return cras_alert_add_callback(state.volume_alert, cb, arg);
}

int cras_system_remove_volume_changed_cb(cras_alert_cb cb, void *arg)
{
	return cras_alert_rm_callback(state.volume_alert, cb, arg);
}

void cras_system_set_capture_gain(long gain)
{
	state.exp_state->capture_gain =
		max(gain, state.exp_state->min_capture_gain);
	cras_alert_pending(state.capture_gain_alert);
}

long cras_system_get_capture_gain()
{
	return state.exp_state->capture_gain;
}

int cras_system_register_capture_gain_changed_cb(cras_alert_cb cb, void *arg)
{
	return cras_alert_add_callback(state.capture_gain_alert, cb, arg);
}

int cras_system_remove_capture_gain_changed_cb(cras_alert_cb cb, void *arg)
{
	return cras_alert_rm_callback(state.capture_gain_alert, cb, arg);
}

void cras_system_set_user_mute(int mute)
{
	state.exp_state->user_mute = !!mute;
	cras_alert_pending(state.mute_alert);
}

void cras_system_set_mute(int mute)
{
	if (state.exp_state->mute_locked)
		return;

	state.exp_state->mute = !!mute;
	cras_alert_pending(state.mute_alert);
}

void cras_system_set_mute_locked(int locked)
{
	state.exp_state->mute_locked = !!locked;

	if (!state.exp_state->mute_locked)
		cras_alert_pending(state.mute_alert);
}

int cras_system_get_mute()
{
	return state.exp_state->mute || state.exp_state->user_mute;
}

int cras_system_get_user_mute()
{
	return state.exp_state->user_mute;
}

int cras_system_get_system_mute()
{
	return state.exp_state->mute;
}

int cras_system_get_mute_locked()
{
	return state.exp_state->mute_locked;
}

int cras_system_register_mute_changed_cb(cras_alert_cb cb, void *arg)
{
	return cras_alert_add_callback(state.mute_alert, cb, arg);
}

int cras_system_remove_mute_changed_cb(cras_alert_cb cb, void *arg)
{
	return cras_alert_rm_callback(state.mute_alert, cb, arg);
}

void cras_system_set_capture_mute(int mute)
{
	if (state.exp_state->capture_mute_locked)
		return;

	state.exp_state->capture_mute = !!mute;
	cras_alert_pending(state.capture_mute_alert);
}

void cras_system_set_capture_mute_locked(int locked)
{
	state.exp_state->capture_mute_locked = !!locked;

	if (!state.exp_state->capture_mute_locked)
		cras_alert_pending(state.capture_mute_alert);
}

int cras_system_get_capture_mute()
{
	return state.exp_state->capture_mute;
}

int cras_system_get_capture_mute_locked()
{
	return state.exp_state->capture_mute_locked;
}

int cras_system_register_capture_mute_changed_cb(cras_alert_cb cb, void *arg)
{
	return cras_alert_add_callback(state.capture_mute_alert, cb, arg);
}

int cras_system_remove_capture_mute_changed_cb(cras_alert_cb cb, void *arg)
{
	return cras_alert_rm_callback(state.capture_mute_alert, cb, arg);
}

void cras_system_set_volume_limits(long min, long max)
{
	state.exp_state->min_volume_dBFS = min;
	state.exp_state->max_volume_dBFS = max;
	cras_alert_pending(state.volume_limits_alert);
}

long cras_system_get_min_volume()
{
	return state.exp_state->min_volume_dBFS;
}

long cras_system_get_max_volume()
{
	return state.exp_state->max_volume_dBFS;
}

int cras_system_register_volume_limits_changed_cb(cras_alert_cb cb, void *arg)
{
	return cras_alert_add_callback(state.volume_limits_alert, cb, arg);
}

int cras_system_remove_volume_limits_changed_cb(cras_alert_cb cb, void *arg)
{
	return cras_alert_rm_callback(state.volume_limits_alert, cb, arg);
}

void cras_system_set_capture_gain_limits(long min, long max)
{
	state.exp_state->min_capture_gain = max(min, DEFAULT_MIN_CAPTURE_GAIN);
	state.exp_state->max_capture_gain = max;
	cras_alert_pending(state.volume_limits_alert);
}

long cras_system_get_min_capture_gain()
{
	return state.exp_state->min_capture_gain;
}

long cras_system_get_max_capture_gain()
{
	return state.exp_state->max_capture_gain;
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

void cras_system_state_stream_added()
{
	struct cras_server_state *s;

	s = cras_system_state_update_begin();
	if (!s)
		return;

	s->num_active_streams++;
	s->num_streams_attached++;

	cras_system_state_update_complete();
	cras_alert_pending(state.active_streams_alert);
}

void cras_system_state_stream_removed()
{
	struct cras_server_state *s;

	s = cras_system_state_update_begin();
	if (!s)
		return;

	/* Set the last active time when removing the final stream. */
	if (s->num_active_streams == 1)
		cras_clock_gettime(CLOCK_MONOTONIC, &s->last_active_stream_time);
	s->num_active_streams--;

	cras_system_state_update_complete();
	cras_alert_pending(state.active_streams_alert);
}

unsigned cras_system_state_get_active_streams()
{
	return state.exp_state->num_active_streams;
}

int cras_system_register_active_streams_changed_cb(cras_alert_cb cb, void *arg)
{
	return cras_alert_add_callback(state.active_streams_alert, cb, arg);
}

int cras_system_remove_active_streams_changed_cb(cras_alert_cb cb, void *arg)
{
	return cras_alert_rm_callback(state.active_streams_alert, cb, arg);
}

void cras_system_state_get_last_stream_active_time(struct cras_timespec *ts)
{
	*ts = state.exp_state->last_active_stream_time;
}

int cras_system_state_get_output_devs(const struct cras_iodev_info **devs)
{
	*devs = state.exp_state->output_devs;
	return state.exp_state->num_output_devs;
}

int cras_system_state_get_input_devs(const struct cras_iodev_info **devs)
{
	*devs = state.exp_state->input_devs;
	return state.exp_state->num_input_devs;
}

int cras_system_state_get_output_nodes(const struct cras_ionode_info **nodes)
{
	*nodes = state.exp_state->output_nodes;
	return state.exp_state->num_output_nodes;
}

int cras_system_state_get_input_nodes(const struct cras_ionode_info **nodes)
{
	*nodes = state.exp_state->input_nodes;
	return state.exp_state->num_input_nodes;
}

struct cras_server_state *cras_system_state_update_begin()
{
	if (pthread_mutex_lock(&state.update_lock)) {
		syslog(LOG_ERR, "Failed to lock stream mutex");
		return NULL;
	}

	__sync_fetch_and_add(&state.exp_state->update_count, 1);
	return state.exp_state;
}

void cras_system_state_update_complete()
{
	__sync_fetch_and_add(&state.exp_state->update_count, 1);
	pthread_mutex_unlock(&state.update_lock);
}

struct cras_server_state *cras_system_state_get_no_lock()
{
	return state.exp_state;
}

key_t cras_sys_state_shm_key()
{
	return state.shm_key;
}

struct cras_tm *cras_system_state_get_tm()
{
	return state.tm;
}
