/* Copyright (c) 2013 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <sys/socket.h>
#include <syslog.h>

#include "cras_hfp_iodev.h"
#include "cras_iodev.h"
#include "cras_iodev_list.h"
#include "cras_util.h"
#include "utlist.h"

#define HFP_BUF_SIZE_BYTES 1024

struct hfp_io {
	struct cras_iodev base;
	struct cras_bt_transport *transport;
	int opened;
};

static int update_supported_formats(struct cras_iodev *iodev)
{
	// 16 bit, mono, 8kHz
	iodev->format->format = SND_PCM_FORMAT_S16_LE;

	free(iodev->supported_rates);
	iodev->supported_rates = (size_t *)malloc(2 * sizeof(size_t));
	iodev->supported_rates[0] = 8000;
	iodev->supported_rates[1] = 0;

	free(iodev->supported_channel_counts);
	iodev->supported_channel_counts = (size_t *)malloc(2 * sizeof(size_t));
	iodev->supported_channel_counts[0] = 1;
	iodev->supported_channel_counts[1] = 0;

	return 0;
}

static int frames_queued(const struct cras_iodev *iodev)
{
	// TODO: Implement this function.
	return 0;
}

static int open_dev(struct cras_iodev *iodev)
{
	struct hfp_io *hfpio = (struct hfp_io *)iodev;
	size_t format_bytes;

	/* Assert format is set before opening device. */
	if (iodev->format == NULL)
		return -EINVAL;
	iodev->format->format = SND_PCM_FORMAT_S16_LE;

	format_bytes = cras_get_format_bytes(iodev->format);
	iodev->buffer_size = HFP_BUF_SIZE_BYTES / format_bytes;
	if (iodev->used_size > iodev->buffer_size)
		iodev->used_size = iodev->buffer_size;

	hfpio->opened = 1;
	return 0;
}

static int close_dev(struct cras_iodev *iodev)
{
	struct hfp_io *hfpio = (struct hfp_io *)iodev;

	hfpio->opened = 0;

	cras_iodev_free_format(iodev);
	return 0;
}

static int is_open(const struct cras_iodev *iodev)
{
	struct hfp_io *hfpio = (struct hfp_io *)iodev;
	return hfpio->opened;
}

static int dev_running(const struct cras_iodev *iodev)
{
	return iodev->is_open(iodev);
}

static int delay_frames(const struct cras_iodev *iodev)
{
	return frames_queued(iodev);
}

static int get_buffer(struct cras_iodev *iodev, uint8_t **dst, unsigned *frames)
{
	// TODO: Implement this function.
	*dst = NULL;
	*frames = 0;

	return 0;
}

static int put_buffer(struct cras_iodev *iodev, unsigned nwritten)
{
	// TODO: Implement this function.
	return 0;
}

static void update_active_node(struct cras_iodev *iodev)
{
}

void hfp_free_resources(struct hfp_io *hfpio)
{
	struct cras_ionode *node;
	node = hfpio->base.active_node;
	if (node) {
		cras_iodev_rm_node(&hfpio->base, node);
		free(node);
	}
	free(hfpio->base.supported_channel_counts);
	free(hfpio->base.supported_rates);
}

struct cras_iodev *hfp_iodev_create(
		enum CRAS_STREAM_DIRECTION dir,
		struct cras_bt_transport *transport)
{
	int err;
	struct hfp_io *hfpio;
	struct cras_iodev *iodev;
	struct cras_ionode *node;
	const char *path;

	hfpio = (struct hfp_io *)calloc(1, sizeof(*hfpio));
	if (!hfpio)
		goto error;

	iodev = &hfpio->base;
	iodev->direction = dir;

	hfpio->transport = transport;
	path = cras_bt_transport_object_path(transport);
	snprintf(iodev->info.name, sizeof(iodev->info.name), "%s:HFP", path);
	iodev->info.name[ARRAY_SIZE(iodev->info.name) - 1] = 0;

	iodev->open_dev= open_dev;
	iodev->is_open = is_open;
	iodev->frames_queued = frames_queued;
	iodev->dev_running = dev_running;
	iodev->delay_frames = delay_frames;
	iodev->get_buffer = get_buffer;
	iodev->put_buffer = put_buffer;
	iodev->close_dev = close_dev;
	iodev->update_supported_formats = update_supported_formats;
	iodev->update_active_node = update_active_node;
	iodev->software_volume_needed = 1;

	node = (struct cras_ionode *)calloc(1, sizeof(*node));
	node->dev = iodev;
	strcpy(node->name, iodev->info.name);

	/* TODO(hychao): set node plugged to 1 to enable HFP iodev */
	node->plugged = 0;
	node->priority = 3;
	node->type = CRAS_NODE_TYPE_BLUETOOTH;
	node->volume = 100;
	gettimeofday(&node->plugged_time, NULL);

	if (dir == CRAS_STREAM_OUTPUT)
		err = cras_iodev_list_add_output(iodev);
	else
		err = cras_iodev_list_add_input(iodev);
	if (err)
		goto error;

	cras_iodev_add_node(iodev, node);
	cras_iodev_set_active_node(iodev, node);

	return iodev;

error:
	if (hfpio) {
		hfp_free_resources(hfpio);
		free(hfpio);
	}
	return NULL;
}

void hfp_iodev_destroy(struct cras_iodev *iodev)
{
	int rc;
	struct hfp_io *hfpio = (struct hfp_io *)iodev;

	if (iodev->direction == CRAS_STREAM_OUTPUT)
		rc = cras_iodev_list_rm_output(iodev);
	else
		rc = cras_iodev_list_rm_input(iodev);
	if (rc == -EBUSY) {
		syslog(LOG_ERR, "Failed to remove iodev %s", iodev->info.name);
		return;
	}
	hfp_free_resources(hfpio);
	free(hfpio);
}
