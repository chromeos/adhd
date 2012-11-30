/* Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <syslog.h>

#include "audio_thread.h"
#include "cras_iodev.h"
#include "cras_iodev_info.h"
#include "cras_iodev_list.h"
#include "cras_rstream.h"
#include "cras_server.h"
#include "cras_types.h"
#include "cras_system_state.h"
#include "utlist.h"

/* Linked list of available devices. */
struct iodev_list {
	struct cras_iodev *iodevs;
	size_t size;
};

/* Separate list for inputs and outputs. */
static struct iodev_list outputs;
static struct iodev_list inputs;
/* Keep a default input and output. */
static struct cras_iodev *default_output;
static struct cras_iodev *default_input;
/* Keep a constantly increasing index for iodevs. */
static size_t next_iodev_idx;

/* Checks if device a is higher priority than b. */
static int dev_is_higher_prio(const struct cras_iodev *a,
			      const struct cras_iodev *b)
{
	/* check if one device is plugged in. */
	if (cras_iodev_is_plugged_in(a) && !cras_iodev_is_plugged_in(b))
		return 1;
	if (!cras_iodev_is_plugged_in(a) && cras_iodev_is_plugged_in(b))
		return 0;

	/* Both plugged or unplugged, check priority. */
	if (a->info.priority > b->info.priority)
		return 1;
	if (a->info.priority < b->info.priority)
		return 0;

	/* Finally check plugged time to break tie. */
	if (cras_iodev_plugged_more_recently(a, b))
		return 1;

	return 0;
}

/* Finds the iodev in the given list that should be used as default.
 * If any devices are "plugged in" (USB, Headphones), route to highest
 * priority plugged device. Break ties with Most recently plugged.  Otherwise
 * route to the highest priority devices, breaking ties with most recently
 * added.
 */
static struct cras_iodev *top_prio_dev(struct cras_iodev *list)
{
	struct cras_iodev *curr;
	struct cras_iodev *ret_dev = NULL;

	DL_FOREACH(list, curr)
		if (!ret_dev || dev_is_higher_prio(curr, ret_dev))
			ret_dev = curr;

	return ret_dev;
}

/* Adds a device to the list.  Used from add_input and add_output. */
static int add_dev_to_list(struct iodev_list *list,
			   struct cras_iodev **default_dev,
			   struct cras_iodev *dev)
{
	struct cras_iodev *tmp, *new_default;
	size_t new_idx;

	DL_FOREACH(list->iodevs, tmp)
		if (tmp == dev)
			return -EEXIST;

	dev->format = NULL;
	dev->prev = dev->next = NULL;

	/* Move to the next index and make sure it isn't taken. */
	new_idx = next_iodev_idx;
	while (1) {
		DL_SEARCH_SCALAR(list->iodevs, tmp, info.idx, new_idx);
		if (tmp == NULL)
			break;
		new_idx++;
	}
	dev->info.idx = new_idx;
	next_iodev_idx = new_idx + 1;
	list->size++;

	syslog(LOG_INFO, "Adding %s dev at index %zu.",
	       dev->direction == CRAS_STREAM_OUTPUT ? "output" : "input",
	       dev->info.idx);
	DL_PREPEND(list->iodevs, dev);
	new_default = top_prio_dev(list->iodevs);
	if (new_default != *default_dev) {
		struct cras_iodev *last;
		last = cras_iodev_set_as_default(new_default->direction,
						 new_default);

		if (last && last->thread)
			audio_thread_rm_all_streams(last->thread);
	}

	cras_iodev_list_update_clients();
	return 0;
}

/* Removes a device to the list.  Used from rm_input and rm_output. */
static int rm_dev_from_list(struct iodev_list *list, struct cras_iodev *dev)
{
	struct cras_iodev *tmp;

	DL_FOREACH(list->iodevs, tmp)
		if (tmp == dev) {
			if (dev->is_open(dev))
				return -EBUSY;
			DL_DELETE(list->iodevs, dev);
			list->size--;
			return 0;
		}

	/* Device not found. */
	return -EINVAL;
}

/* Fills a dev_info array from the iodev_list. */
static void fill_dev_list(struct iodev_list *list,
			  struct cras_iodev_info *dev_info,
			  size_t out_size)
{
	int i = 0;
	struct cras_iodev *tmp;
	DL_FOREACH(list->iodevs, tmp) {
		memcpy(&dev_info[i], &tmp->info, sizeof(dev_info[0]));
		i++;
		if (i == out_size)
			return;
	}
}

/* Copies the info for each device in the list to "list_out". */
static int get_dev_list(struct iodev_list *list,
			struct cras_iodev_info **list_out)
{
	struct cras_iodev_info *dev_info;

	*list_out = NULL;
	if (list->size == 0)
		return 0;

	dev_info = malloc(sizeof(*list_out[0]) * list->size);
	if (dev_info == NULL)
		return -ENOMEM;

	fill_dev_list(list, dev_info, list->size);

	*list_out = dev_info;
	return list->size;
}

/* Called when the system volume changes.  Pass the current volume setting to
 * the default output if it is active. */
void sys_vol_change(void *data)
{
	if (default_output &&
	    default_output->set_volume &&
	    default_output->is_open(default_output))
		default_output->set_volume(default_output);
}

/* Called when the system mute state changes.  Pass the current mute setting
 * to the default output if it is active. */
void sys_mute_change(void *data)
{
	if (default_output &&
	    default_output->set_mute &&
	    default_output->is_open(default_output))
		default_output->set_mute(default_output);
}

/* Called when the system capture gain changes.  Pass the current capture_gain
 * setting to the default input if it is active. */
void sys_cap_gain_change(void *data)
{
	if (default_input &&
	    default_input->set_capture_gain &&
	    default_input->is_open(default_input))
		default_input->set_capture_gain(default_input);
}

/* Called when the system capture mute state changes.  Pass the current capture
 * mute setting to the default input if it is active. */
void sys_cap_mute_change(void *data)
{
	if (default_input &&
	    default_input->set_capture_mute &&
	    default_input->is_open(default_input))
		default_input->set_capture_mute(default_input);
}

/*
 * Exported Interface.
 */

void cras_iodev_list_init()
{
	cras_system_register_volume_changed_cb(sys_vol_change, NULL);
	cras_system_register_mute_changed_cb(sys_mute_change, NULL);
	cras_system_register_capture_gain_changed_cb(sys_cap_gain_change, NULL);
	cras_system_register_capture_mute_changed_cb(sys_cap_mute_change, NULL);
}

void cras_iodev_list_deinit()
{
	cras_system_remove_volume_changed_cb(sys_vol_change, NULL);
	cras_system_remove_mute_changed_cb(sys_vol_change, NULL);
	cras_system_remove_capture_gain_changed_cb(sys_cap_gain_change, NULL);
	cras_system_remove_capture_mute_changed_cb(sys_cap_mute_change, NULL);
}

/* Finds the current device for a stream of "type", only default streams are
 * currently supported so return the default device fot the given direction.
 */
int cras_get_iodev_for_stream_type(enum CRAS_STREAM_TYPE type,
				   enum CRAS_STREAM_DIRECTION direction,
				   struct cras_iodev **idev,
				   struct cras_iodev **odev)
{
	switch (direction) {
	case CRAS_STREAM_OUTPUT:
		*idev = NULL;
		*odev = default_output;
		break;
	case CRAS_STREAM_INPUT:
		*idev = default_input;
		*odev = NULL;
		break;
	case CRAS_STREAM_UNIFIED:
		*idev = default_input;
		*odev = default_output;
		break;
	default:
		return -EINVAL;
	}
	return 0;
}

struct cras_iodev *cras_iodev_set_as_default(
		enum CRAS_STREAM_DIRECTION dir,
		struct cras_iodev *new_default)
{
	struct cras_iodev *old_default;
	struct cras_iodev **curr;

	curr = (dir == CRAS_STREAM_OUTPUT) ? &default_output : &default_input;

	/* Set current default to the newly requested device. */
	old_default = *curr;
	*curr = new_default;

	if (new_default && new_default->set_as_default)
		new_default->set_as_default(new_default);

	return old_default;
}

int cras_iodev_list_add_output(struct cras_iodev *output)
{
	if (output->direction != CRAS_STREAM_OUTPUT)
		return -EINVAL;

	return add_dev_to_list(&outputs, &default_output, output);
}

int cras_iodev_list_add_input(struct cras_iodev *input)
{
	if (input->direction != CRAS_STREAM_INPUT)
		return -EINVAL;

	return add_dev_to_list(&inputs, &default_input, input);
}

int cras_iodev_list_rm_output(struct cras_iodev *dev)
{
	int res;

	if (dev->thread)
		audio_thread_rm_all_streams(dev->thread);
	res = rm_dev_from_list(&outputs, dev);
	if (default_output == dev)
		cras_iodev_set_as_default(CRAS_STREAM_OUTPUT,
					  top_prio_dev(outputs.iodevs));
	if (res == 0)
		cras_iodev_list_update_clients();
	return res;
}

int cras_iodev_list_rm_input(struct cras_iodev *dev)
{
	int res;

	if (dev->thread)
		audio_thread_rm_all_streams(dev->thread);
	res = rm_dev_from_list(&inputs, dev);
	if (default_input == dev)
		cras_iodev_set_as_default(CRAS_STREAM_INPUT,
					  top_prio_dev(inputs.iodevs));
	if (res == 0)
		cras_iodev_list_update_clients();
	return res;
}

int cras_iodev_list_get_outputs(struct cras_iodev_info **list_out)
{
	return get_dev_list(&outputs, list_out);
}

int cras_iodev_list_get_inputs(struct cras_iodev_info **list_out)
{
	return get_dev_list(&inputs, list_out);
}

int cras_iodev_move_stream_type(enum CRAS_STREAM_TYPE type, size_t index)
{
	struct cras_iodev *curr_dev, *new_dev;

	/* Find new dev */
	DL_SEARCH_SCALAR(outputs.iodevs, new_dev, info.idx, index);
	if (new_dev == NULL) {
		DL_SEARCH_SCALAR(inputs.iodevs, new_dev, info.idx, index);
		if (new_dev == NULL)
			return -EINVAL;
	}

	/* Set default to the newly requested device. */
	curr_dev = cras_iodev_set_as_default(new_dev->direction, new_dev);

	if (curr_dev == NULL || curr_dev == new_dev)
		return 0; /* No change or no streams to move. */

	if (curr_dev->thread)
		audio_thread_rm_all_streams(curr_dev->thread);

	return 0;
}

int cras_iodev_move_stream_type_top_prio(enum CRAS_STREAM_TYPE type,
					 enum CRAS_STREAM_DIRECTION direction)
{
	struct iodev_list *list;
	struct cras_iodev *to_switch, *curr_default;

	if (direction == CRAS_STREAM_OUTPUT) {
		list = &outputs;
		curr_default = default_output;
	} else {
		assert(direction == CRAS_STREAM_INPUT);
		list = &inputs;
		curr_default = default_input;
	}

	to_switch = top_prio_dev(list->iodevs);

	/* If there is no iodev to switch to, or if we are already using the
	 * default, then there is nothing else to do. */
	if (!to_switch || to_switch == curr_default)
		return 0;

	syslog(LOG_DEBUG, "Route to %zu by default", to_switch->info.idx);

	/* There is an iodev and it isn't the default, switch to it. */
	return cras_iodev_move_stream_type(type, to_switch->info.idx);
}

void cras_iodev_list_update_clients()
{
	struct cras_server_state *state;

	state = cras_system_state_update_begin();
	if (!state)
		return;

	state->num_output_devs = outputs.size;
	state->num_input_devs = inputs.size;
	fill_dev_list(&outputs, &state->output_devs[0], CRAS_MAX_IODEVS);
	fill_dev_list(&inputs, &state->input_devs[0], CRAS_MAX_IODEVS);

	cras_system_state_update_complete();
}

struct audio_thread *
cras_iodev_list_get_audio_thread(const struct cras_iodev *iodev)
{
	return iodev->thread;
}
