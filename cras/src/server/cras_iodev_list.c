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
/* Keep a constantly increasing index for iodevs. Index 0 is unused. */
static uint32_t next_iodev_idx = 1;
/* Selected node for input and output. 0 if there is no node selected. */
static cras_node_id_t selected_input;
static cras_node_id_t selected_output;

static struct cras_iodev *find_dev(size_t dev_index)
{
	struct cras_iodev *dev;

	DL_FOREACH(outputs.iodevs, dev)
		if (dev->info.idx == dev_index)
			return dev;

	DL_FOREACH(inputs.iodevs, dev)
		if (dev->info.idx == dev_index)
			return dev;

	return NULL;
}

static struct cras_ionode *find_node(cras_node_id_t id)
{
	struct cras_iodev *dev;
	struct cras_ionode *node;
	uint32_t dev_index, node_index;

	dev_index = dev_index_of(id);
	node_index = node_index_of(id);

	dev = find_dev(dev_index);
	if (!dev)
		return NULL;

	DL_FOREACH(dev->nodes, node)
		if (node->idx == node_index)
			return node;

	return NULL;
}

/* Checks if device a is higher priority than b. */
static int dev_is_higher_prio(const struct cras_iodev *a,
			      const struct cras_iodev *b)
{
	if (cras_ionode_better(a->active_node, b->active_node))
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
	uint32_t new_idx;

	DL_FOREACH(list->iodevs, tmp)
		if (tmp == dev)
			return -EEXIST;

	dev->format = NULL;
	dev->prev = dev->next = NULL;

	/* Move to the next index and make sure it isn't taken. */
	new_idx = next_iodev_idx;
	while (1) {
		/* Index 0 is reserved to mean "no device". */
		if (new_idx == 0)
			new_idx++;
		DL_SEARCH_SCALAR(list->iodevs, tmp, info.idx, new_idx);
		if (tmp == NULL)
			break;
		new_idx++;
	}
	dev->info.idx = new_idx;
	next_iodev_idx = new_idx + 1;
	list->size++;

	syslog(LOG_INFO, "Adding %s dev at index %u.",
	       dev->direction == CRAS_STREAM_OUTPUT ? "output" : "input",
	       dev->info.idx);
	DL_PREPEND(list->iodevs, dev);
	new_default = top_prio_dev(list->iodevs);
	if (new_default != *default_dev) {
		struct cras_iodev *last;
		last = cras_iodev_set_as_default(new_default->direction,
						 new_default);

		if (last && last->thread)
			audio_thread_destroy(last->thread);
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

/* Fills an ionode_info array from the iodev_list. */
static int fill_node_list(struct iodev_list *list,
			  struct cras_ionode_info *node_info,
			  size_t out_size)
{
	int i = 0;
	struct cras_iodev *dev;
	struct cras_ionode *node;
	DL_FOREACH(list->iodevs, dev) {
		DL_FOREACH(dev->nodes, node) {
			node_info->iodev_idx = dev->info.idx;
			node_info->ionode_idx = node->idx;
			node_info->priority = node->priority;
			node_info->plugged = node->plugged;
			node_info->plugged_time = node->plugged_time;
			node_info->active =
				(dev == default_input || dev == default_output)
				&& (dev->active_node == node);
			strcpy(node_info->name, node->name);
			node_info++;
			i++;
			if (i == out_size)
				return i;
		}
	}
	return i;
}

/* Copies the info for each device in the list to "list_out". */
static int get_dev_list(struct iodev_list *list,
			struct cras_iodev_info **list_out)
{
	struct cras_iodev_info *dev_info;

	if (!list_out)
		return list->size;

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
		audio_thread_destroy(dev->thread);
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
		audio_thread_destroy(dev->thread);
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

int cras_iodev_move_stream_type(enum CRAS_STREAM_TYPE type, uint32_t index)
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
		audio_thread_destroy(curr_dev->thread);

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

	syslog(LOG_DEBUG, "Route to %u by default", to_switch->info.idx);

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

	state->num_output_nodes = fill_node_list(&outputs,
						 &state->output_nodes[0],
						 CRAS_MAX_IONODES);
	state->num_input_nodes = fill_node_list(&inputs,
						&state->input_nodes[0],
						CRAS_MAX_IONODES);

	cras_system_state_update_complete();
}

struct audio_thread *
cras_iodev_list_get_audio_thread(const struct cras_iodev *iodev)
{
	return iodev->thread;
}

static void select_node(cras_node_id_t node_id, struct cras_ionode *ionode,
			enum ionode_attr attr, int value)
{
	struct cras_iodev *iodev = ionode->dev;
	struct cras_iodev *old_dev = NULL;
	cras_node_id_t *selected;

	selected = (iodev->direction == CRAS_STREAM_OUTPUT) ? &selected_output :
		&selected_input;

	if (value) {
		if (node_id == *selected) /* already selected, return */
			return;
		old_dev = find_dev(dev_index_of(*selected));
		*selected = node_id;
		iodev->update_active_node(iodev);
		if (old_dev && old_dev != iodev)
			old_dev->update_active_node(iodev);
	} else {
		if (node_id != *selected) /* not selected, return */
			return;
		*selected = 0;
		iodev->update_active_node(iodev);
	}
}

int cras_iodev_list_set_node_attr(cras_node_id_t node_id,
				  enum ionode_attr attr, int value)
{
	struct cras_ionode *node;

	node = find_node(node_id);
	if (!node)
		return -EINVAL;

	if (attr == IONODE_ATTR_SELECTED) {
		select_node(node_id, node, attr, value);
		return 0;
	} else {
		return cras_iodev_set_node_attr(node, attr, value);
	}
}

int cras_iodev_list_node_selected(struct cras_ionode *node)
{
	cras_node_id_t id = cras_make_node_id(node->dev->info.idx, node->idx);
	return (id == selected_input || id == selected_output);
}

void cras_iodev_list_clear_selection(enum CRAS_STREAM_DIRECTION direction)
{
	cras_node_id_t *selected;
	struct cras_iodev *iodev;

	selected = (direction == CRAS_STREAM_OUTPUT) ? &selected_output :
		&selected_input;

	if (!*selected)
		return;

	iodev = find_dev(dev_index_of(*selected));
	*selected = 0;
	if (iodev)
		iodev->update_active_node(iodev);
}
