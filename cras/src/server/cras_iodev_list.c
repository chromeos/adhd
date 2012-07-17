/* Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <syslog.h>

#include "cras_iodev.h"
#include "cras_iodev_info.h"
#include "cras_iodev_list.h"
#include "cras_rstream.h"
#include "cras_server.h"
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

/* Finds a device that is currently playing a stream of "type".  If none is
 * found, then return NULL. */
static struct cras_iodev *get_curr_iodev_for_stream_type(
		struct iodev_list *list,
		enum CRAS_STREAM_TYPE type)
{
	struct cras_iodev *dev;
	struct cras_io_stream *iostream;

	DL_FOREACH(list->iodevs, dev) {
		if (!dev->streams)
			continue;
		DL_FOREACH(dev->streams, iostream) {
			struct cras_rstream *stream = iostream->stream;
			if (stream && cras_rstream_get_type(stream) == type)
				return dev;
		}
	}
	return NULL;
}

/* Adds a device to the list.  Used from add_input and add_output. */
static int add_dev_to_list(struct iodev_list *list,
			   struct cras_iodev *dev)
{
	struct cras_iodev *tmp;
	size_t new_idx;

	DL_FOREACH(list->iodevs, tmp)
		if (tmp == dev)
			return -EEXIST;

	dev->format = NULL;
	dev->streams = NULL;
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

	return 0;
}

/* Removes a device to the list.  Used from rm_input and rm_output. */
static int rm_dev_from_list(struct iodev_list *list, struct cras_iodev *dev)
{
	struct cras_iodev *tmp;

	DL_FOREACH(list->iodevs, tmp)
		if (tmp == dev) {
			if (cras_iodev_streams_attached(dev))
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

/* Finds the supported sample rate that best suits the requested rate, "rrate".
 * Exact matches have highest priority, then integer multiples, then the default
 * rate for the device. */
static size_t get_best_rate(struct cras_iodev *iodev, size_t rrate)
{
	size_t i;
	size_t best;

	if (iodev->supported_rates[0] == 0) /* No rates supported */
		return 0;

	for (i = 0, best = 0; iodev->supported_rates[i] != 0; i++) {
		if (rrate == iodev->supported_rates[i])
			return rrate;
		if (best == 0 && (rrate % iodev->supported_rates[i] == 0 ||
				  iodev->supported_rates[i] % rrate == 0))
			best = iodev->supported_rates[i];
	}

	if (best)
		return best;
	return iodev->supported_rates[0];
}

/* Finds the best match for the channel count.  This will return an exact match
 * only, if there is no exact match, it falls back to the default channel count
 * for the device (The first in the list). */
static size_t get_best_channel_count(struct cras_iodev *iodev, size_t count)
{
	size_t i;

	assert(iodev->supported_channel_counts[0] != 0);

	for (i = 0; iodev->supported_channel_counts[i] != 0; i++) {
		if (iodev->supported_channel_counts[i] == count)
			return count;
	}
	return iodev->supported_channel_counts[0];
}

/* Re-orders the list of devices based on the priority of each device.  Places
 * the devices with higher priority at the beginning of the list.  It is
 * important that the relative order of devices with the same priority is
 * preserved (Within a priority the most recent devices will be chosen).
 */
static void sort_iodev_list(struct cras_iodev **list)
{
	struct cras_iodev *dev, *tmp;
	struct cras_iodev *old_list = *list;
	struct cras_iodev *new_list = NULL;

	dev = old_list;
	if (dev == NULL)
		return; /* Nothing to sort. */

	/* Move the first device. */
	DL_DELETE(old_list, dev);
	DL_APPEND(new_list, dev);

	/* Then the insert the rest in the correct order. */
	DL_FOREACH_SAFE(old_list, dev, tmp) {
		struct cras_iodev *curr;

		/* Remove from the old list before adding to the new list. */
		DL_DELETE(old_list, dev);

		/* Check if higher priority than head. */
		if (dev->info.priority > new_list->info.priority) {
			DL_PREPEND(new_list, dev);
			continue;
		}

		/* Check if lower priority than the tail. */
		if (dev->info.priority <= new_list->prev->info.priority) {
			DL_APPEND(new_list, dev);
			continue;
		}

		/* Not highest or lowest, insert in the list. */
		DL_FOREACH(new_list, curr) {
			if (dev->info.priority > curr->info.priority) {
				dev->prev = curr->prev;
				dev->next = curr;
				dev->prev->next = dev;
				curr->prev = dev;
				break;
			}
		}
	}

	*list = new_list;
}

/*
 * Exported Functions.
 */

/* Finds the current device for a stream of "type", if there isn't one fall
 * back to the default input or output device depending on "direction" */
struct cras_iodev *cras_get_iodev_for_stream_type(
		enum CRAS_STREAM_TYPE type,
		enum CRAS_STREAM_DIRECTION direction)
{
	struct cras_iodev *dev, *def;
	struct iodev_list *list;

	if (direction == CRAS_STREAM_OUTPUT) {
		list = &outputs;
		def = default_output;
	} else {
		list = &inputs;
		def = default_input;
	}

	dev = get_curr_iodev_for_stream_type(list, type);
	if (dev == NULL)
		return def;
	return dev;
}

int cras_iodev_list_add_output(struct cras_iodev *output, int auto_route)
{
	int rc;

	if (output->direction != CRAS_STREAM_OUTPUT)
		return -EINVAL;

	rc = add_dev_to_list(&outputs, output);
	if (rc < 0)
		return rc;
	if (default_output == NULL) {
		/* First output, make it default regardless. */
		default_output = output;
		DL_APPEND(outputs.iodevs, output);
		return cras_iodev_list_update_clients();
	}
	syslog(LOG_DEBUG, "Adding iodev at index %zu.", output->info.idx);
	if (auto_route) {
		/* auto-route devices go to the front of the list. */
		DL_PREPEND(outputs.iodevs, output);
		struct cras_iodev *last_default = default_output;
		default_output = output;
		cras_iodev_remove_all_streams(last_default);
		syslog(LOG_DEBUG, "Default output dev %zu.", output->info.idx);
	} else
		DL_APPEND(outputs.iodevs, output);
	return cras_iodev_list_update_clients();
}

int cras_iodev_list_add_input(struct cras_iodev *input, int auto_route)
{
	int rc;

	if (input->direction != CRAS_STREAM_INPUT)
		return -EINVAL;

	rc = add_dev_to_list(&inputs, input);
	if (rc < 0)
		return rc;
	if (default_input == NULL) {
		/* First input, make it default regardless. */
		default_input = input;
		DL_APPEND(inputs.iodevs, input);
		return cras_iodev_list_update_clients();
	}
	syslog(LOG_DEBUG, "Adding iodev at index %zu.", input->info.idx);
	if (auto_route) {
		/* auto-route devices go to the front of the list. */
		DL_PREPEND(inputs.iodevs, input);
		struct cras_iodev *last_default = default_input;
		default_input = input;
		cras_iodev_remove_all_streams(last_default);
		syslog(LOG_DEBUG, "Default input dev %zu.", input->info.idx);
	} else
		DL_APPEND(inputs.iodevs, input);
	return cras_iodev_list_update_clients();
}

int cras_iodev_list_rm_output(struct cras_iodev *dev)
{
	int res;

	if (default_output == dev) {
		if (dev == outputs.iodevs)
			default_output = outputs.iodevs->next;
		else
			default_output = outputs.iodevs;
	}
	cras_iodev_remove_all_streams(dev);
	res = rm_dev_from_list(&outputs, dev);
	if (res == 0)
		res = cras_iodev_list_update_clients();
	return res;
}

int cras_iodev_list_rm_input(struct cras_iodev *dev)
{
	int res;

	if (default_input == dev)
		default_input = inputs.iodevs->next;
	cras_iodev_remove_all_streams(dev);
	res = rm_dev_from_list(&inputs, dev);
	if (res == 0)
		res = cras_iodev_list_update_clients();
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

/* Informs the rstream who belongs to it, and then tell the device that it
 * now owns the stream. */
int cras_iodev_attach_stream(struct cras_iodev *iodev,
			     struct cras_rstream *stream)
{
	cras_rstream_set_iodev(stream, iodev);
	return iodev->add_stream(iodev, stream);
}

/* Removes the stream from the device and tells the stream structure that it is
 * no longer owned. */
int cras_iodev_detach_stream(struct cras_iodev *iodev,
			     struct cras_rstream *stream)
{
	int rc;

	rc = iodev->rm_stream(iodev, stream);
	cras_rstream_set_iodev(stream, NULL);
	return rc;
}

int cras_iodev_set_format(struct cras_iodev *iodev,
			  struct cras_audio_format *fmt)
{
	size_t actual_rate, actual_num_channels;

	/* If this device isn't already using a format, try to match the one
	 * requested in "fmt". */
	if (iodev->format == NULL) {
		iodev->format = malloc(sizeof(struct cras_audio_format));
		if (!iodev->format)
			return -ENOMEM;
		*iodev->format = *fmt;
		actual_rate = get_best_rate(iodev, fmt->frame_rate);
		actual_num_channels = get_best_channel_count(iodev,
							     fmt->num_channels);
		if (actual_rate == 0 || actual_num_channels == 0) {
			/* No compatible frame rate found. */
			free(iodev->format);
			iodev->format = NULL;
			return -EINVAL;
		}
		iodev->format->frame_rate = actual_rate;
		iodev->format->num_channels = actual_num_channels;
		/* TODO(dgreid) - allow other formats. */
		iodev->format->format = SND_PCM_FORMAT_S16_LE;
	}

	*fmt = *(iodev->format);
	return 0;
}

int cras_iodev_move_stream_type(enum CRAS_STREAM_TYPE type, size_t index)
{
	struct cras_iodev *curr_dev, *new_dev;
	struct cras_io_stream *iostream, *tmp;

	/* Find new dev */
	DL_SEARCH_SCALAR(outputs.iodevs, new_dev, info.idx, index);
	if (new_dev == NULL) {
		DL_SEARCH_SCALAR(inputs.iodevs, new_dev, info.idx, index);
		if (new_dev == NULL)
			return -EINVAL;
	}

	/* Set default to the newly requested device. */
	if (new_dev->direction == CRAS_STREAM_OUTPUT) {
		curr_dev = default_output;
		default_output = new_dev;
	} else {
		curr_dev = default_input;
		default_input = new_dev;
	}
	if (curr_dev == NULL || curr_dev == new_dev)
		return 0; /* No change or no streams to move. */

	/* For each stream on curr, detach and tell client to reconfig. */
	DL_FOREACH_SAFE(curr_dev->streams, iostream, tmp) {
		struct cras_rstream *stream = iostream->stream;
		if (cras_rstream_get_type(stream) == type) {
			cras_iodev_detach_stream(curr_dev, stream);
			cras_rstream_send_client_reattach(stream);
		}
	}

	return 0;
}

int cras_iodev_move_stream_type_default(enum CRAS_STREAM_TYPE type,
					enum CRAS_STREAM_DIRECTION direction)
{
	struct iodev_list *list;
	struct cras_iodev *curr_default;

	if (direction == CRAS_STREAM_OUTPUT) {
		list = &outputs;
		curr_default = default_output;
	} else {
		assert(direction == CRAS_STREAM_INPUT);
		list = &inputs;
		curr_default = default_input;
	}

	/* If there is no iodev to switch to, or if we are already using the
	 * default, then there is nothing else to do. */
	if (list->iodevs == NULL || list->iodevs == curr_default)
		return 0;
	/* There is an iodev and it isn't the default, switch to it. */
	return cras_iodev_move_stream_type(type, list->iodevs->info.idx);
}

void cras_iodev_remove_all_streams(struct cras_iodev *dev)
{
	struct cras_io_stream *iostream, *tmp;

	DL_FOREACH_SAFE(dev->streams, iostream, tmp) {
		struct cras_rstream *stream = iostream->stream;
		cras_iodev_detach_stream(dev, stream);
		cras_rstream_send_client_reattach(stream);
	}
}

void cras_iodev_sort_device_lists()
{
	int auto_route;

	sort_iodev_list(&inputs.iodevs);
	default_input = inputs.iodevs;

	/* If playing to default then switch to
	 * new if it changes. */
	auto_route = default_output == outputs.iodevs;
	sort_iodev_list(&outputs.iodevs);
	if (auto_route)
		default_output = outputs.iodevs;
}

/* Sends out the list of iodevs in the system. */
int cras_iodev_list_update_clients()
{
	size_t msg_size;
	struct cras_client_iodev_list *msg;

	msg_size = sizeof(*msg) +
		sizeof(struct cras_iodev_info) * (outputs.size + inputs.size);
	msg = malloc(msg_size);
	if (msg == NULL)
		return -ENOMEM;

	msg->num_outputs = outputs.size;
	msg->num_inputs = inputs.size;
	fill_dev_list(&outputs, &msg->iodevs[0], outputs.size);
	fill_dev_list(&inputs, &msg->iodevs[outputs.size], inputs.size);
	msg->header.length = msg_size;
	msg->header.id = CRAS_CLIENT_IODEV_LIST;

	cras_server_send_to_all_clients(&msg->header);
	free(msg);
	return 0;
}
