/* Copyright (c) 2013 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <sys/socket.h>
#include <syslog.h>

#include "cras_audio_area.h"
#include "cras_hfp_iodev.h"
#include "cras_hfp_info.h"
#include "cras_iodev.h"
#include "cras_system_state.h"
#include "cras_util.h"
#include "utlist.h"


struct hfp_io {
	struct cras_iodev base;
	struct cras_bt_device *device;
	struct hfp_info *info;
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

	free(iodev->supported_formats);
	iodev->supported_formats =
		(snd_pcm_format_t *)malloc(2 * sizeof(snd_pcm_format_t));
	iodev->supported_formats[0] = SND_PCM_FORMAT_S16_LE;
	iodev->supported_formats[1] = 0;

	return 0;
}

static int frames_queued(const struct cras_iodev *iodev)
{
	struct hfp_io *hfpio = (struct hfp_io *)iodev;

	if (!hfp_info_running(hfpio->info))
		return -1;

	return hfp_buf_queued(hfpio->info, iodev);
}

static int open_dev(struct cras_iodev *iodev)
{
	struct hfp_io *hfpio = (struct hfp_io *)iodev;
	int sk, err, mtu;

	/* Assert format is set before opening device. */
	if (iodev->format == NULL)
		return -EINVAL;
	iodev->format->format = SND_PCM_FORMAT_S16_LE;
	cras_iodev_init_audio_area(iodev, iodev->format->num_channels);

	if (hfp_info_running(hfpio->info))
		goto add_dev;

	sk = cras_bt_device_sco_connect(hfpio->device);
	if (sk < 0)
		goto error;

	mtu = cras_bt_device_sco_mtu(hfpio->device, sk);

	/* Start hfp_info */
	err = hfp_info_start(sk, mtu, hfpio->info);
	if (err)
		goto error;

add_dev:
	hfp_info_add_iodev(hfpio->info, iodev);

	iodev->buffer_size = hfp_buf_size(hfpio->info, iodev);
	hfpio->opened = 1;

	return 0;
error:
	syslog(LOG_ERR, "Failed to open HFP iodev");
	return -1;
}

static int close_dev(struct cras_iodev *iodev)
{
	struct hfp_io *hfpio = (struct hfp_io *)iodev;

	hfpio->opened = 0;
	hfp_info_rm_iodev(hfpio->info, iodev);
	if (hfp_info_running(hfpio->info) && !hfp_info_has_iodev(hfpio->info))
		hfp_info_stop(hfpio->info);

	cras_iodev_free_format(iodev);
	cras_iodev_free_audio_area(iodev);
	return 0;
}

static void set_hfp_volume(struct cras_iodev *iodev)
{
	size_t volume;
	struct hfp_io *hfpio = (struct hfp_io *)iodev;

	volume = cras_system_get_volume();
	if (iodev->active_node)
		volume = cras_iodev_adjust_node_volume(iodev->active_node, volume);

	cras_bt_device_set_speaker_gain(hfpio->device, volume);
}

static int is_open(const struct cras_iodev *iodev)
{
	struct hfp_io *hfpio = (struct hfp_io *)iodev;
	return hfpio->opened && hfp_info_running(hfpio->info);
}

static int dev_running(const struct cras_iodev *iodev)
{
	return iodev->is_open(iodev);
}

static int delay_frames(const struct cras_iodev *iodev)
{
	return frames_queued(iodev);
}

static int get_buffer(struct cras_iodev *iodev,
		      struct cras_audio_area **area,
		      unsigned *frames)
{
	struct hfp_io *hfpio = (struct hfp_io *)iodev;
	uint8_t *dst = NULL;

	if (!hfp_info_running(hfpio->info))
		return -1;

	hfp_buf_acquire(hfpio->info, iodev, &dst, frames);

	iodev->area->frames = *frames;
	/* HFP is mono only. */
	iodev->area->channels[0].step_bytes =
		cras_get_format_bytes(iodev->format);
	iodev->area->channels[0].buf = dst;

	*area = iodev->area;
	return 0;
}

static int put_buffer(struct cras_iodev *iodev, unsigned nwritten)
{
	struct hfp_io *hfpio = (struct hfp_io *)iodev;

	if (!hfp_info_running(hfpio->info))
		return -1;

	hfp_buf_release(hfpio->info, iodev, nwritten);
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
		struct cras_bt_device *device,
		enum cras_bt_device_profile profile,
		struct hfp_info *info)
{
	struct hfp_io *hfpio;
	struct cras_iodev *iodev;
	struct cras_ionode *node;

	hfpio = (struct hfp_io *)calloc(1, sizeof(*hfpio));
	if (!hfpio)
		goto error;

	iodev = &hfpio->base;
	iodev->direction = dir;

	hfpio->device = device;

	/* Set iodev's name to device readable name or the address. */
	snprintf(iodev->info.name, sizeof(iodev->info.name), "%s",
		 cras_bt_device_name(device));

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
	iodev->set_volume = set_hfp_volume;

	node = (struct cras_ionode *)calloc(1, sizeof(*node));
	node->dev = iodev;
	strcpy(node->name, iodev->info.name);

	node->plugged = 1;
	node->type = CRAS_NODE_TYPE_BLUETOOTH;
	node->volume = 100;
	gettimeofday(&node->plugged_time, NULL);

	cras_bt_device_append_iodev(device, iodev, profile);
	cras_iodev_add_node(iodev, node);
	cras_iodev_set_active_node(iodev, node);

	hfpio->info = info;

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
	struct hfp_io *hfpio = (struct hfp_io *)iodev;

	cras_bt_device_rm_iodev(hfpio->device, iodev);
	hfp_free_resources(hfpio);
	free(hfpio);
}
