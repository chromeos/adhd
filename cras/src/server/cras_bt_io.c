/* Copyright (c) 2014 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <syslog.h>

#include "cras_bt_io.h"
#include "cras_iodev.h"
#include "cras_iodev_list.h"
#include "utlist.h"

/* Extends cras_ionode to hold bluetooth profile information
 * so that iodevs of different profile(A2DP or HFP/HSP) can be
 * associated with the same bt_io.
 * Members:
 *    base - The base class cras_ionode.
 *    profile_dev - Pointer to the profile specific iodev.
 *    profile - The bluetooth profile profile_dev runs on.
 */
struct bt_node {
	struct cras_ionode base;
	struct cras_iodev *profile_dev;
	enum cras_bt_device_profile profile;
};

/* The structure represents a virtual input or output device of a
 * bluetooth audio device, speaker or headset for example. A node
 * will be added to this virtual iodev for each profile supported
 * by the bluetooth audio device.
 * Member:
 *    base - The base class cras_iodev
 */
struct bt_io {
	struct cras_iodev base;
	struct cras_bt_device *device;
};

/* Returns the active profile specific iodev. */
static struct cras_iodev *active_profile_dev(const struct cras_iodev *iodev)
{
	struct bt_node *n = (struct bt_node *)iodev->active_node;

	return n ? n->profile_dev : NULL;
}

/* Adds a profile specific iodev to btio. */
static struct cras_ionode *add_profile_dev(struct cras_iodev *bt_iodev,
					   struct cras_iodev *dev,
					   enum cras_bt_device_profile profile)
{
	struct bt_node *n;

	n = (struct bt_node *)calloc(1, sizeof(*n));
	if (!n)
		return NULL;

	n->base.dev = bt_iodev;
	n->base.type = CRAS_NODE_TYPE_BLUETOOTH;
	n->base.volume = 100;
	gettimeofday(&n->base.plugged_time, NULL);

	strcpy(n->base.name, dev->info.name);
	n->profile_dev = dev;
	n->profile = profile;

	cras_iodev_add_node(bt_iodev, &n->base);
	return &n->base;
}

static int update_supported_formats(struct cras_iodev *iodev)
{
	struct cras_iodev *dev = active_profile_dev(iodev);
	int rc, length, i;

	if (dev->format == NULL) {
		dev->format = (struct cras_audio_format *)
				malloc(sizeof(*dev->format));
		*dev->format = *iodev->format;
	}

	if (dev->update_supported_formats) {
		rc = dev->update_supported_formats(dev);
		if (rc)
			return rc;
	}

	/* Fill in the supported rates and channel counts. */
	for (length = 0; dev->supported_rates[length]; length++);
	free(iodev->supported_rates);
	iodev->supported_rates = (size_t *)malloc(
			(length + 1) * sizeof(*iodev->supported_rates));
	for (i = 0; i < length + 1; i++)
		iodev->supported_rates[i] = dev->supported_rates[i];

	for (length = 0; dev->supported_channel_counts[length]; length++);
	iodev->supported_channel_counts = (size_t *)malloc(
		(length + 1) * sizeof(*iodev->supported_channel_counts));
	for (i = 0; i < length + 1; i++)
		iodev->supported_channel_counts[i] =
			dev->supported_channel_counts[i];

	for (length = 0; dev->supported_formats[length]; length++);
	iodev->supported_formats = (snd_pcm_format_t *)malloc(
		(length + 1) * sizeof(*iodev->supported_formats));
	for (i = 0; i < length + 1; i++)
		iodev->supported_formats[i] =
			dev->supported_formats[i];
	return 0;
}

static int open_dev(struct cras_iodev *iodev)
{
	int rc;
	struct cras_iodev *dev = active_profile_dev(iodev);
	if (!dev)
		return -EINVAL;

	/* Fill back the format iodev is using. */
	*dev->format = *iodev->format;

	rc = dev->open_dev(dev);
	if (rc)
		return rc;

	iodev->buffer_size = dev->buffer_size;
	iodev->min_buffer_level = dev->min_buffer_level;
	return 0;
}

static int close_dev(struct cras_iodev *iodev)
{
	int rc;
	struct bt_node *active;
	struct cras_iodev *dev;

	active = (struct bt_node *)iodev->active_node;
	dev = active->profile_dev;

	rc = dev->close_dev(dev);
	if (rc < 0)
		return rc;
	cras_iodev_free_format(iodev);
	return 0;
}

static int is_open(const struct cras_iodev *iodev)
{
	struct cras_iodev *dev = active_profile_dev(iodev);
	if (!dev)
		return 0;
	return dev->is_open(dev);
}

static int frames_queued(const struct cras_iodev *iodev)
{
	struct cras_iodev *dev = active_profile_dev(iodev);
	if (!dev)
		return -EINVAL;
	return dev->frames_queued(dev);
}

static int dev_running(const struct cras_iodev *iodev)
{
	struct cras_iodev *dev = active_profile_dev(iodev);
	if (!dev)
		return -EINVAL;
	return dev->dev_running(dev);
}

static int delay_frames(const struct cras_iodev *iodev)
{
	struct cras_iodev *dev = active_profile_dev(iodev);
	if (!dev)
		return -EINVAL;
	return dev->delay_frames(dev);
}

static int get_buffer(struct cras_iodev *iodev,
		      struct cras_audio_area **area,
		      unsigned *frames)
{
	struct cras_iodev *dev = active_profile_dev(iodev);
	if (!dev)
		return -EINVAL;
	return dev->get_buffer(dev, area, frames);
}

static int put_buffer(struct cras_iodev *iodev, unsigned nwritten)
{
	struct cras_iodev *dev = active_profile_dev(iodev);
	if (!dev)
		return -EINVAL;
	return dev->put_buffer(dev, nwritten);
}

static void update_active_node(struct cras_iodev *iodev)
{
	struct cras_iodev *dev = active_profile_dev(iodev);
	if (!dev)
		return;
	dev->update_active_node(dev);
}

struct cras_iodev *cras_bt_io_create(struct cras_bt_device *device,
				     struct cras_iodev *dev,
				     enum cras_bt_device_profile profile)
{
	int err;
	struct bt_io *btio;
	struct cras_iodev *iodev;
	struct cras_ionode *node;

	if (!dev)
		return NULL;

	btio = (struct bt_io *)calloc(1, sizeof(*btio));
	if (!btio)
		goto error;
	btio->device = device;

	iodev = &btio->base;
	iodev->direction = dev->direction;
	strcpy(iodev->info.name, dev->info.name);

	iodev->open_dev = open_dev;
	iodev->is_open = is_open; /* Needed by thread_add_stream */
	iodev->frames_queued = frames_queued;
	iodev->dev_running = dev_running;
	iodev->delay_frames = delay_frames;
	iodev->get_buffer = get_buffer;
	iodev->put_buffer = put_buffer;
	iodev->close_dev = close_dev;
	iodev->update_supported_formats = update_supported_formats;
	iodev->update_active_node = update_active_node;
	iodev->software_volume_needed = 1;

	node = add_profile_dev(&btio->base, dev, profile);
	if (node == NULL)
		goto error;

	if (iodev->direction == CRAS_STREAM_OUTPUT)
		err = cras_iodev_list_add_output(iodev);
	else
		err = cras_iodev_list_add_input(iodev);
	if (err)
		goto error;

	node->plugged = 1;
	cras_iodev_set_active_node(iodev, node);
	return &btio->base;

error:
	if (btio)
		free(btio);
	return NULL;
}

void cras_bt_io_destroy(struct cras_iodev *bt_iodev)
{
	int rc;
	struct bt_io *btio = (struct bt_io *)bt_iodev;
	struct cras_ionode *node;
	struct bt_node *n;

	if (bt_iodev->direction == CRAS_STREAM_OUTPUT)
		rc = cras_iodev_list_rm_output(bt_iodev);
	else
		rc = cras_iodev_list_rm_input(bt_iodev);
	if (rc == -EBUSY)
		return;

	DL_FOREACH(bt_iodev->nodes, node) {
		n = (struct bt_node *)node;
		cras_iodev_rm_node(bt_iodev, node);
		free(n);
	}
	free(btio);
}

int cras_bt_io_has_dev(struct cras_iodev *bt_iodev,
			  struct cras_iodev *dev)
{
	struct cras_ionode *node;

	DL_FOREACH(bt_iodev->nodes, node) {
		struct bt_node *n = (struct bt_node *)node;
		if (n->profile_dev == dev)
			return 1;
	}
	return 0;
}

int cras_bt_io_append(struct cras_iodev *bt_iodev,
		      struct cras_iodev *dev,
		      enum cras_bt_device_profile profile)
{
	struct cras_ionode *node;

	if (cras_bt_io_has_dev(bt_iodev, dev))
		return -EEXIST;
	node = add_profile_dev(bt_iodev, dev, profile);
	if (!node)
		return -ENOMEM;
	return 0;
}

int cras_bt_io_remove(struct cras_iodev *bt_iodev,
		      struct cras_iodev *dev)
{
	struct cras_ionode *node;
	struct bt_node *btnode;

	DL_SEARCH_SCALAR_WITH_CAST(bt_iodev->nodes, node, btnode,
				   profile_dev, dev);
	if (node) {
		DL_DELETE(bt_iodev->nodes, node);
		free(node);
	}
	return 0;
}
