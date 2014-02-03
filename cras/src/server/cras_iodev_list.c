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
#include "softvol_curve.h"
#include "utlist.h"

/* Linked list of available devices. */
struct iodev_list {
	struct cras_iodev *iodevs;
	size_t size;
};

/* Separate list for inputs and outputs. */
static struct iodev_list outputs;
static struct iodev_list inputs;
/* Keep an active input and output. */
static struct cras_iodev *active_output;
static struct cras_iodev *active_input;
/* Keep a default input and output. For use when there is nothing else. */
static struct cras_iodev *default_output;
static struct cras_iodev *default_input;
/* device used for loopback. */
static struct cras_iodev *loopback_dev;
/* Keep a constantly increasing index for iodevs. Index 0 is reserved
 * to mean "no device". */
static uint32_t next_iodev_idx = 1;
/* Selected node for input and output. 0 if there is no node selected. */
static cras_node_id_t selected_input;
static cras_node_id_t selected_output;
/* Called when the nodes are added/removed. */
static struct cras_alert *nodes_changed_alert;
/* Called when the active output/input is changed */
static struct cras_alert *active_node_changed_alert;
/* Call when the volume of a node changes. */
static node_volume_callback_t node_volume_callback;
static node_volume_callback_t node_input_gain_callback;
/* Thread that handles audio input and output. */
static struct audio_thread *audio_thread;

static void nodes_changed_prepare(struct cras_alert *alert);
static void active_node_changed_prepare(struct cras_alert *alert);

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

/* Adds a device to the list.  Used from add_input and add_output. */
static int add_dev_to_list(struct iodev_list *list,
			   struct cras_iodev *dev)
{
	struct cras_iodev *tmp;
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

	cras_iodev_list_update_device_list();
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

static const char *node_type_to_str(enum CRAS_NODE_TYPE type)
{
	switch (type) {
	case CRAS_NODE_TYPE_INTERNAL_SPEAKER:
		return "INTERNAL_SPEAKER";
	case CRAS_NODE_TYPE_HEADPHONE:
		return "HEADPHONE";
	case CRAS_NODE_TYPE_HDMI:
		return "HDMI";
	case CRAS_NODE_TYPE_INTERNAL_MIC:
		return "INTERNAL_MIC";
	case CRAS_NODE_TYPE_MIC:
		return "MIC";
	case CRAS_NODE_TYPE_USB:
		return "USB";
	case CRAS_NODE_TYPE_BLUETOOTH:
		return "BLUETOOTH";
	case CRAS_NODE_TYPE_UNKNOWN:
	default:
		return "UNKNOWN";
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
			node_info->plugged_time.tv_sec =
				node->plugged_time.tv_sec;
			node_info->plugged_time.tv_usec =
				node->plugged_time.tv_usec;
			node_info->active =
				(dev == active_input || dev == active_output)
				&& (dev->active_node == node);
			node_info->volume = node->volume;
			node_info->capture_gain = node->capture_gain;
			strcpy(node_info->name, node->name);
			snprintf(node_info->type, sizeof(node_info->type), "%s",
				node_type_to_str(node->type));
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

static void update_software_volume(struct cras_iodev *iodev)
{
	if (cras_iodev_software_volume_needed(iodev)) {
		unsigned int volume = cras_system_get_volume();
		float volume_scaler = softvol_get_scaler(
			cras_iodev_adjust_active_node_volume(iodev, volume));
		cras_iodev_set_software_volume(iodev, volume_scaler);
	}
	else {
		cras_iodev_set_software_volume(iodev, 1.0);
	}
}

/* Called when the system volume changes.  Pass the current volume setting to
 * the default output if it is active. */
void sys_vol_change(void *data)
{
	if (active_output &&
	    active_output->set_volume &&
	    active_output->is_open(active_output))
		active_output->set_volume(active_output);
	update_software_volume(active_output);
}

/* Called when the system mute state changes.  Pass the current mute setting
 * to the default output if it is active. */
void sys_mute_change(void *data)
{
	if (active_output &&
	    active_output->set_mute &&
	    active_output->is_open(active_output))
		active_output->set_mute(active_output);
}

/* Called when the system capture gain changes.  Pass the current capture_gain
 * setting to the default input if it is active. */
void sys_cap_gain_change(void *data)
{
	if (active_input &&
	    active_input->set_capture_gain &&
	    active_input->is_open(active_input))
		active_input->set_capture_gain(active_input);
}

/* Called when the system capture mute state changes.  Pass the current capture
 * mute setting to the default input if it is active. */
void sys_cap_mute_change(void *data)
{
	if (active_input &&
	    active_input->set_capture_mute &&
	    active_input->is_open(active_input))
		active_input->set_capture_mute(active_input);
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
	nodes_changed_alert = cras_alert_create(nodes_changed_prepare);
	active_node_changed_alert = cras_alert_create(
		active_node_changed_prepare);
	audio_thread = audio_thread_create();
	audio_thread_start(audio_thread);
}

void cras_iodev_list_deinit()
{
	cras_system_remove_volume_changed_cb(sys_vol_change, NULL);
	cras_system_remove_mute_changed_cb(sys_vol_change, NULL);
	cras_system_remove_capture_gain_changed_cb(sys_cap_gain_change, NULL);
	cras_system_remove_capture_mute_changed_cb(sys_cap_mute_change, NULL);
	cras_alert_destroy(nodes_changed_alert);
	cras_alert_destroy(active_node_changed_alert);
	nodes_changed_alert = NULL;
	active_node_changed_alert = NULL;
	audio_thread_destroy(audio_thread);
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
		*odev = active_output;
		break;
	case CRAS_STREAM_INPUT:
		*idev = active_input;
		*odev = NULL;
		break;
	case CRAS_STREAM_UNIFIED:
		*idev = active_input;
		*odev = active_output;
		break;
	case CRAS_STREAM_POST_MIX_PRE_DSP:
		*idev = (struct cras_iodev *)loopback_dev;
		*odev = NULL;
		break;
	default:
		return -EINVAL;
	}
	return 0;
}

static struct cras_iodev *cras_iodev_set_active(
		enum CRAS_STREAM_DIRECTION dir,
		struct cras_iodev *new_active)
{
	struct cras_iodev *old_active;
	struct cras_iodev **curr;

	if (new_active && new_active->set_as_default)
		new_active->set_as_default(new_active);

	cras_iodev_list_notify_active_node_changed();

	audio_thread_remove_streams(audio_thread, dir);

	if (dir == CRAS_STREAM_OUTPUT) {
		curr = &active_output;
		audio_thread_set_output_dev(audio_thread, new_active);
	} else {
		curr = &active_input;
		audio_thread_set_input_dev(audio_thread, new_active);
	}

	/* Set current active to the newly requested device. */
	old_active = *curr;
	*curr = new_active;

	return old_active;
}

int cras_iodev_list_add_output(struct cras_iodev *output)
{
	int rc;

	if (output->direction != CRAS_STREAM_OUTPUT)
		return -EINVAL;

	rc = add_dev_to_list(&outputs, output);
	if (rc)
		return rc;

	if (!active_output) {
		active_output = output;
		audio_thread_set_output_dev(audio_thread, output);
	}
	if (!default_output)
		default_output = output;

	return 0;
}

int cras_iodev_list_add_input(struct cras_iodev *input)
{
	int rc;

	if (input->direction == CRAS_STREAM_POST_MIX_PRE_DSP) {
		loopback_dev = input;
		audio_thread_add_loopback_device(audio_thread, input);
		return 0;
	}

	if (input->direction != CRAS_STREAM_INPUT)
		return -EINVAL;

	rc = add_dev_to_list(&inputs, input);
	if (rc)
		return rc;

	if (!active_input) {
		active_input = input;
		audio_thread_set_input_dev(audio_thread, input);
	}
	if (!default_input)
		default_input = input;

	return 0;
}

int cras_iodev_list_rm_output(struct cras_iodev *dev)
{
	int res;

	/* Retire the current active output device before removing it from
	 * list, otherwise it could be busy and remain in the list.
	 */
	if (active_output == dev)
		cras_iodev_set_active(CRAS_STREAM_OUTPUT, default_output);

	res = rm_dev_from_list(&outputs, dev);
	if (res == 0)
		cras_iodev_list_update_device_list();
	return res;
}

int cras_iodev_list_rm_input(struct cras_iodev *dev)
{
	int res;

	/* Retire the current active input device before removing it from
	 * list, otherwise it could be busy and remain in the list.
	 */
	if (active_input == dev)
		cras_iodev_set_active(CRAS_STREAM_INPUT, default_input);

	res = rm_dev_from_list(&inputs, dev);
	if (res == 0)
		cras_iodev_list_update_device_list();
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

cras_node_id_t cras_iodev_list_get_active_node_id(
	enum CRAS_STREAM_DIRECTION direction)
{
	struct cras_iodev *dev = (direction == CRAS_STREAM_OUTPUT) ?
		active_output : active_input;

	if (!dev || !dev->active_node)
		return 0;

	return cras_make_node_id(dev->info.idx, dev->active_node->idx);
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
	curr_dev = cras_iodev_set_active(new_dev->direction, new_dev);

	if (curr_dev == NULL || curr_dev == new_dev)
		return 0; /* No change or no streams to move. */

	return 0;
}

void cras_iodev_list_update_device_list()
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
	state->selected_output = selected_output;
	state->selected_input = selected_input;

	cras_system_state_update_complete();
}

int cras_iodev_list_register_nodes_changed_cb(cras_alert_cb cb, void *arg)
{
	return cras_alert_add_callback(nodes_changed_alert, cb, arg);
}

int cras_iodev_list_remove_nodes_changed_cb(cras_alert_cb cb, void *arg)
{
	return cras_alert_rm_callback(nodes_changed_alert, cb, arg);
}

void cras_iodev_list_notify_nodes_changed()
{
	cras_alert_pending(nodes_changed_alert);
}

static void nodes_changed_prepare(struct cras_alert *alert)
{
	cras_iodev_list_update_device_list();
}

int cras_iodev_list_register_active_node_changed_cb(cras_alert_cb cb,
						    void *arg)
{
	return cras_alert_add_callback(active_node_changed_alert, cb, arg);
}

int cras_iodev_list_remove_active_node_changed_cb(cras_alert_cb cb,
						  void *arg)
{
	return cras_alert_rm_callback(active_node_changed_alert, cb, arg);
}

void cras_iodev_list_notify_active_node_changed()
{
	cras_alert_pending(active_node_changed_alert);
	if (active_output)
		update_software_volume(active_output);
}

static void active_node_changed_prepare(struct cras_alert *alert)
{
	cras_iodev_list_update_device_list();
}

void cras_iodev_list_select_node(enum CRAS_STREAM_DIRECTION direction,
				 cras_node_id_t node_id)
{
	struct cras_iodev *old_dev = NULL, *new_dev = NULL;
	cras_node_id_t *selected;

	selected = (direction == CRAS_STREAM_OUTPUT) ? &selected_output :
		&selected_input;

	/* return if no change */
	if (node_id == *selected)
		return;

	/* find the devices for the id. */
	old_dev = find_dev(dev_index_of(*selected));
	new_dev = find_dev(dev_index_of(node_id));

	/* Fail if the direction is mismatched. We don't fail for the new_dev ==
	   NULL case. That can happen if node_id is 0 (no selection), or the
	   client tries to select a non-existing node (maybe it's unplugged just
	   before the client selects it). We will just behave like there is no
	   selected node. */
	if (new_dev && new_dev->direction != direction)
		return;

	/* change to new selection */
	*selected = node_id;

	/* update new device */
	if (new_dev) {
		new_dev->update_active_node(new_dev);
		/* There is an iodev and it isn't the default, switch to it. */
		cras_iodev_move_stream_type(CRAS_STREAM_TYPE_DEFAULT, new_dev->info.idx);
	}

	/* update old device if it is not the same device */
	if (old_dev && old_dev != new_dev)
		old_dev->update_active_node(old_dev);
}

int cras_iodev_list_set_node_attr(cras_node_id_t node_id,
				  enum ionode_attr attr, int value)
{
	struct cras_ionode *node;

	node = find_node(node_id);
	if (!node)
		return -EINVAL;

	return cras_iodev_set_node_attr(node, attr, value);
}

int cras_iodev_list_node_selected(struct cras_ionode *node)
{
	cras_node_id_t id = cras_make_node_id(node->dev->info.idx, node->idx);
	return (id == selected_input || id == selected_output);
}

void cras_iodev_list_set_node_volume_callbacks(node_volume_callback_t volume_cb,
					       node_volume_callback_t gain_cb)
{
	node_volume_callback = volume_cb;
	node_input_gain_callback = gain_cb;
}

void cras_iodev_list_notify_node_volume(struct cras_ionode *node)
{
	cras_node_id_t id = cras_make_node_id(node->dev->info.idx, node->idx);

	if (node_volume_callback)
		node_volume_callback(id, node->volume);

	update_software_volume(node->dev);
}

void cras_iodev_list_notify_node_capture_gain(struct cras_ionode *node)
{
	cras_node_id_t id = cras_make_node_id(node->dev->info.idx, node->idx);

	if (node_input_gain_callback)
		node_input_gain_callback(id, node->capture_gain);
}

struct audio_thread *cras_iodev_list_get_audio_thread()
{
	return audio_thread;
}

void cras_iodev_list_reset()
{
	active_output = NULL;
	active_input = NULL;
	default_output = NULL;
	default_input = NULL;
}
