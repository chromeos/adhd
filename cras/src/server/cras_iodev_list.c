/* Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "cras_iodev.h"
#include "cras_iodev_info.h"
#include "cras_iodev_list.h"
#include "cras_rstream.h"
#include "utlist.h"

/* Linked list of available devices. */
struct iodev_list {
	struct cras_iodev *iodevs;
	size_t next_idx;
	size_t size;
};

/* Separate list for inputs and outputs. */
static struct iodev_list outputs;
static struct iodev_list inputs;
/* Keep a default input and output. */
static struct cras_iodev *default_output;
static struct cras_iodev *default_input;

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
	new_idx = list->next_idx;
	while (1) {
		DL_SEARCH_SCALAR(list->iodevs, tmp, info.idx, new_idx);
		if (tmp == NULL)
			break;
		new_idx++;
	}
	dev->info.idx = new_idx;
	list->next_idx = new_idx + 1;
	list->size++;

	DL_APPEND(list->iodevs, dev);
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

/* Copies the info for each device in the list to "list_out". */
static int get_dev_list(struct iodev_list *list,
			struct cras_iodev_info **list_out)
{
	int i;
	struct cras_iodev_info *dev_info;
	struct cras_iodev *tmp;

	*list_out = NULL;

	if (list->size == 0)
		return 0;

	dev_info = malloc(sizeof(*list_out[0]) * list->size);
	if (dev_info == NULL)
		return -ENOMEM;

	i = 0;
	DL_FOREACH(list->iodevs, tmp) {
		memcpy(&dev_info[i], &tmp->info, sizeof(dev_info[0]));
		i++;
	}

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

int cras_iodev_list_add_output(struct cras_iodev *output)
{
	int rc;

	rc = add_dev_to_list(&outputs, output);
	if (rc < 0)
		return rc;
	if (default_output == NULL)
		default_output = output;
	return 0;
}

int cras_iodev_list_add_input(struct cras_iodev *input)
{
	int rc;

	rc = add_dev_to_list(&inputs, input);
	if (rc < 0)
		return rc;
	if (default_input == NULL)
		default_input = input;
	return 0;
}

int cras_iodev_list_rm_output(struct cras_iodev *dev)
{
	if (default_output == dev)
		default_output = outputs.iodevs->next;
	return rm_dev_from_list(&outputs, dev);
}

int cras_iodev_list_rm_input(struct cras_iodev *dev)
{
	if (default_input == dev)
		default_input = inputs.iodevs->next;
	return rm_dev_from_list(&inputs, dev);
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
	size_t actual_rate;

	/* If this device isn't already using a format, try to match the one
	 * requested in "fmt". */
	if (iodev->format == NULL) {
		iodev->format = malloc(sizeof(struct cras_audio_format));
		if (!iodev->format)
			return -ENOMEM;
		*iodev->format = *fmt;
		actual_rate = get_best_rate(iodev, fmt->frame_rate);
		if (actual_rate == 0) {
			/* No compatible frame rate found. */
			free(iodev->format);
			iodev->format = NULL;
			return -EINVAL;
		}
		iodev->format->frame_rate = actual_rate;
	}

	*fmt = *(iodev->format);
	return 0;
}

int cras_iodev_move_stream_type(enum CRAS_STREAM_TYPE type, size_t index)
{
	struct cras_iodev *curr_dev, *new_dev;
	struct iodev_list *list;
	struct cras_io_stream *iostream, *tmp;

	/* Find the stream type's current io device. */
	list = &outputs;
	curr_dev = get_curr_iodev_for_stream_type(list, type);
	if (curr_dev == NULL) {
		list = &inputs;
		curr_dev = get_curr_iodev_for_stream_type(list, type);
	}
	if (curr_dev == NULL)
		return 0; /* No streams to move. */

	/* Find new dev */
	DL_SEARCH_SCALAR(list->iodevs, new_dev, info.idx, index);
	if (!new_dev)
		return -EINVAL;

	/* Set default to the newly requested device. */
	if (list == &outputs)
		default_output = new_dev;
	else
		default_input = new_dev;

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
