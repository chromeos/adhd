/* Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <alsa/asoundlib.h>
#include <syslog.h>

#include "cras_alsa_jack.h"
#include "cras_alsa_mixer.h"
#include "cras_system_state.h"
#include "cras_util.h"
#include "utlist.h"

/* Keeps an fd that is registered with system settings.  A list of fds must be
 * kept so that they can be removed when the jack list is destroyed. */
struct jack_poll_fd {
	int fd;
	struct jack_poll_fd *prev, *next;
};

/* Represents a single alsa Jack, e.g. "Headphone Jack" or "Mic Jack".
 *    elem - alsa hcontrol element for this jack.
 *    jack_list - list of jacks this belongs to.
 *    mixer_output - mixer output control used to control audio to this jack.
 *        This will be null for input jacks.
 */
struct cras_alsa_jack {
	snd_hctl_elem_t *elem;
	struct cras_alsa_jack_list *jack_list;
	struct cras_alsa_mixer_output *mixer_output;
	struct cras_alsa_jack *prev, *next;
};

/* Contains all Jacks for a given device.
 *    hctl - alsa hcontrol for this device.
 *    mixer - cras mixer for the card providing this device.
 *    device_index - Index ALSA uses to refer to the device.  The Y in "hw:X,Y".
 *    registered_fds - list of fds registered with system, to be removed upon
 *        destruction.
 *    change_callback - function to call when the state of a jack changes.
 *    callback_data - data to pass back to the callback.
 *    jacks - list of jacks for this device.
 */
struct cras_alsa_jack_list {
	snd_hctl_t *hctl;
	struct cras_alsa_mixer *mixer;
	size_t device_index;
	struct jack_poll_fd *registered_fds;
	jack_state_change_callback *change_callback;
	void *callback_data;
	struct cras_alsa_jack *jacks;
};

/*
 * Local Helpers.
 */

/* Callback from alsa when a jack control changes.  This is registered with
 * snd_hctl_elem_set_callback in find_jack_controls and run by calling
 * snd_hctl_handle_events in alsa_control_event_pending below.
 * Args:
 *    elem - The ALSA control element that has changed.
 *    mask - unused.
 */
static int hctl_jack_cb(snd_hctl_elem_t *elem, unsigned int mask)
{
	const char *name;
	snd_ctl_elem_value_t *elem_value;
	struct cras_alsa_jack *jack;

	jack = snd_hctl_elem_get_callback_private(elem);
	if (jack == NULL) {
		syslog(LOG_ERR, "Invalid jack from control event.");
		return -EINVAL;
	}

	snd_ctl_elem_value_alloca(&elem_value);
	snd_hctl_elem_read(elem, elem_value);
	name = snd_hctl_elem_get_name(elem);

	syslog(LOG_DEBUG,
	       "Jack %s %s",
	       name,
	       snd_ctl_elem_value_get_boolean(elem_value, 0) ? "plugged"
							     : "unplugged");

	jack->jack_list->change_callback(
			jack,
			snd_ctl_elem_value_get_boolean(elem_value, 0),
			jack->jack_list->callback_data);
	return 0;
}

/* Handles notifications from alsa controls.  Called by main thread when a poll
 * fd provided by alsa signals there is an event available. */
static void alsa_control_event_pending(void *arg)
{
	struct cras_alsa_jack_list *jack_list;

	jack_list = (struct cras_alsa_jack_list *)arg;
	if (jack_list == NULL) {
		syslog(LOG_ERR, "Invalid jack_list from control event.");
		return;
	}

	/* handle_events will trigger the callback registered with each control
	 * that has changed. */
	snd_hctl_handle_events(jack_list->hctl);
}

/* Checks if the given control name is in the supplied list of possible jack
 * control names. */
static int is_jack_control_in_list(const char * const *list,
				   unsigned int list_length,
				   const char *control_name)
{
	unsigned int i;

	for (i = 0; i < list_length; i++)
		if (strcmp(control_name, list[i]) == 0)
			return 1;
	return 0;
}

/* Registers each poll fd (one per jack) with the system so that they are passed
 * to select in the main loop. */
static int add_jack_poll_fds(struct cras_alsa_jack_list *jack_list)
{
	struct pollfd *pollfds;
	nfds_t n;
	unsigned int i;
	int rc = 0;

	n = snd_hctl_poll_descriptors_count(jack_list->hctl);
	if (n == 0)
		return 0;

	pollfds = malloc(n * sizeof(*pollfds));
	if (pollfds == NULL)
		return -ENOMEM;

	n = snd_hctl_poll_descriptors(jack_list->hctl, pollfds, n);
	for (i = 0; i < n; i++) {
		struct jack_poll_fd *registered_fd;

		registered_fd = calloc(1, sizeof(*registered_fd));
		if (registered_fd == NULL) {
			rc = -ENOMEM;
			break;
		}
		registered_fd->fd = pollfds[i].fd;
		DL_APPEND(jack_list->registered_fds, registered_fd);
		rc = cras_system_add_select_fd(registered_fd->fd,
					       alsa_control_event_pending,
					       jack_list);
		if (rc < 0)
			break;
	}
	free(pollfds);
	return rc;
}

/* Cancels registration of each poll fd (one per jack) with the system. */
static void remove_jack_poll_fds(struct cras_alsa_jack_list *jack_list)
{
	struct jack_poll_fd *registered_fd, *tmp;

	DL_FOREACH_SAFE(jack_list->registered_fds, registered_fd, tmp) {
		cras_system_rm_select_fd(registered_fd->fd);
		DL_DELETE(jack_list->registered_fds, registered_fd);
		free(registered_fd);
	}
}

/* Looks for any JACK controls.  Monitors any found controls for changes and
 * decides to route based on plug/unlpug events. */
static int find_jack_controls(struct cras_alsa_jack_list *jack_list,
			      const char *device_name,
			      enum CRAS_STREAM_DIRECTION direction)
{
	int rc;
	snd_hctl_elem_t *elem;
	static const char * const output_jack_names[] = {
		"Headphone Jack",
		"Front Headphone Jack",
	};
	static const char * const input_jack_names[] = {
		"Mic Jack",
	};
	const char * const *jack_names;
	unsigned int num_jack_names;

	if (direction == CRAS_STREAM_OUTPUT) {
		jack_names = output_jack_names;
		num_jack_names = ARRAY_SIZE(output_jack_names);
	} else {
		assert(direction == CRAS_STREAM_INPUT);
		jack_names = input_jack_names;
		num_jack_names = ARRAY_SIZE(input_jack_names);
	}

	rc = snd_hctl_open(&jack_list->hctl, device_name, SND_CTL_NONBLOCK);
	if (rc < 0) {
		syslog(LOG_ERR, "failed to get hctl for %s", device_name);
		return rc;
	}
	rc = snd_hctl_nonblock(jack_list->hctl, 1);
	if (rc < 0) {
		syslog(LOG_ERR, "failed to nonblock hctl for %s", device_name);
		return rc;
	}
	rc = snd_hctl_load(jack_list->hctl);
	if (rc < 0) {
		syslog(LOG_ERR, "failed to load hctl for %s", device_name);
		return rc;
	}
	for (elem = snd_hctl_first_elem(jack_list->hctl); elem != NULL;
			elem = snd_hctl_elem_next(elem)) {
		snd_ctl_elem_iface_t iface;
		const char *name;
		struct cras_alsa_jack *jack;

		iface = snd_hctl_elem_get_interface(elem);
		if (iface != SND_CTL_ELEM_IFACE_CARD)
			continue;
		name = snd_hctl_elem_get_name(elem);
		if (!is_jack_control_in_list(jack_names, num_jack_names, name))
			continue;

		jack = calloc(1, sizeof(*jack));
		if (jack == NULL)
			return -ENOMEM;
		jack->elem = elem;
		jack->jack_list = jack_list;
		DL_APPEND(jack_list->jacks, jack);

		syslog(LOG_DEBUG, "Found Jack: %s for %s", name, device_name);
		snd_hctl_elem_set_callback(elem, hctl_jack_cb);
		snd_hctl_elem_set_callback_private(elem, jack);

		if (direction == CRAS_STREAM_OUTPUT)
			jack->mixer_output =
				cras_alsa_mixer_get_output_matching_name(
					jack_list->mixer,
					jack_list->device_index,
					name);
	}

	/* If we have found jacks, have the poll fds passed to select in the
	 * main loop. */
	if (jack_list->jacks != NULL) {
		rc = add_jack_poll_fds(jack_list);
		if (rc < 0)
			return rc;
	}

	return 0;
}

/*
 * Exported Interface.
 */

struct cras_alsa_jack_list *cras_alsa_jack_list_create(
		unsigned int card_index,
		unsigned int device_index,
		struct cras_alsa_mixer *mixer,
		enum CRAS_STREAM_DIRECTION direction,
		jack_state_change_callback *cb,
		void *cb_data)
{
	struct cras_alsa_jack_list *jack_list;
	char device_name[6];

	/* Enforce alsa limits. */
	assert(card_index < 32);
	assert(device_index < 32);

	jack_list = (struct cras_alsa_jack_list *)calloc(1, sizeof(*jack_list));
	if (jack_list == NULL)
		return NULL;

	jack_list->change_callback = cb;
	jack_list->callback_data = cb_data;
	jack_list->mixer = mixer;
	jack_list->device_index = device_index;

	snprintf(device_name, sizeof(device_name), "hw:%d", card_index);

	if (find_jack_controls(jack_list, device_name, direction) != 0) {
		cras_alsa_jack_list_destroy(jack_list);
		return NULL;
	}

	return jack_list;
}

void cras_alsa_jack_list_destroy(struct cras_alsa_jack_list *jack_list)
{
	struct cras_alsa_jack *jack, *tmp;

	if (jack_list == NULL)
		return;
	remove_jack_poll_fds(jack_list);
	DL_FOREACH_SAFE(jack_list->jacks, jack, tmp) {
		DL_DELETE(jack_list->jacks, jack);
		free(jack);
	}
	if (jack_list->hctl)
		snd_hctl_close(jack_list->hctl);
	free(jack_list);
}

struct cras_alsa_mixer_output *cras_alsa_jack_get_mixer_output(
		const struct cras_alsa_jack *jack)
{
	if (jack == NULL)
		return NULL;
	return jack->mixer_output;
}
