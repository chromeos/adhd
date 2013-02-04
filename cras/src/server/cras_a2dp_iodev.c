/* Copyright (c) 2013 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <stdint.h>
#include <sys/socket.h>
#include <syslog.h>
#include <time.h>

#include "cras_a2dp_info.h"
#include "cras_a2dp_iodev.h"
#include "cras_iodev.h"
#include "cras_iodev_list.h"
#include "cras_util.h"
#include "rtp.h"
#include "utlist.h"

#define PCM_BUF_MAX_SIZE_BYTES 1024

struct a2dp_io {
	struct cras_iodev base;
	struct a2dp_info a2dp;
	struct cras_bt_transport *transport;

	uint8_t pcm_buf[PCM_BUF_MAX_SIZE_BYTES];
	int pcm_buf_offset;
	int pcm_buf_size;
	int pcm_buf_used;

	int bt_queued_frames;
	struct timespec bt_queued_fr_last_update;
};

static int update_supported_formats(struct cras_iodev *iodev)
{
	struct a2dp_io *a2dpio = (struct a2dp_io *)iodev;
	size_t rate = 0;
	size_t channel;
	a2dp_sbc_t a2dp;

	cras_bt_transport_configuration(a2dpio->transport, &a2dp,
					sizeof(a2dp));

	iodev->format->format = SND_PCM_FORMAT_S16_LE;
	channel = (a2dp.channel_mode == SBC_CHANNEL_MODE_MONO) ? 1 : 2;

	if (a2dp.frequency & SBC_SAMPLING_FREQ_48000)
		rate = 48000;
	else if (a2dp.frequency & SBC_SAMPLING_FREQ_44100)
		rate = 44100;
	else if (a2dp.frequency & SBC_SAMPLING_FREQ_32000)
		rate = 32000;
	else if (a2dp.frequency & SBC_SAMPLING_FREQ_16000)
		rate = 16000;

	iodev->supported_rates = (size_t *)malloc(2 * sizeof(rate));
	iodev->supported_rates[0] = rate;
	iodev->supported_rates[1] = 0;

	iodev->supported_channel_counts = (size_t *)malloc(2 * sizeof(channel));
	iodev->supported_channel_counts[0] = channel;
	iodev->supported_channel_counts[1] = 0;

	return 0;
}

/* Calculates the amount of consumed frames since given time.
 */
static unsigned long frames_since(struct timespec ts, size_t rate)
{
	struct timespec te, diff;

	clock_gettime(CLOCK_MONOTONIC, &te);
	subtract_timespecs(&te, &ts, &diff);
	return diff.tv_sec * rate + diff.tv_nsec / (1000000000L / rate);
}

/* Maintains a virtual buffer for transmitted frames, assume this buffer is
 * consumed at the same frame rate at bluetooth device side.
 * Args:
 *    iodev: The a2dp iodev to estimate the queued frames for.
 *    fr: The amount of frames just transmitted.
 */
static int get_bt_queued_frames(const struct cras_iodev *iodev, int fr)
{
	unsigned long consumed;
	struct a2dp_io *a2dpio = (struct a2dp_io *)iodev;

	/* Calculate consumed frames since last update time, also update
	 * the last update time.
	 */
	consumed = frames_since(a2dpio->bt_queued_fr_last_update,
				iodev->format->frame_rate);
	clock_gettime(CLOCK_MONOTONIC, &a2dpio->bt_queued_fr_last_update);

	if (a2dpio->bt_queued_frames > consumed)
		a2dpio->bt_queued_frames -= consumed;
	else
		a2dpio->bt_queued_frames = 0;

	a2dpio->bt_queued_frames += fr;

	return a2dpio->bt_queued_frames;
}

static int frames_queued(const struct cras_iodev *iodev)
{
	size_t format_bytes;
	int frames;
	struct a2dp_io *a2dpio = (struct a2dp_io *)iodev;

	format_bytes = cras_get_format_bytes(iodev->format);

	frames = a2dpio->pcm_buf_used / format_bytes
			+ a2dp_queued_frames(&a2dpio->a2dp)
			+ get_bt_queued_frames(iodev, 0);

	return frames;
}


static int open_dev(struct cras_iodev *iodev)
{
	int err = 0, block_size;
	size_t format_bytes;
	struct a2dp_io *a2dpio = (struct a2dp_io *)iodev;

	err = cras_bt_transport_acquire(a2dpio->transport);
	if (err < 0) {
		syslog(LOG_ERR, "transport_acquire failed");
		return err;
	}

	/* Assert format is set before opening device. */
	if (iodev->format == NULL)
		return -EINVAL;
	iodev->format->format = SND_PCM_FORMAT_S16_LE;
	format_bytes = cras_get_format_bytes(iodev->format);

	/* Back calculate the max size of a2dp block before encode. */
	block_size = a2dp_block_size(
			&a2dpio->a2dp,
			cras_bt_transport_write_mtu(a2dpio->transport) -
					sizeof(struct rtp_header) -
					sizeof(struct rtp_payload));

	/* Assert pcm_buf_size be multiple of codesize */
	a2dpio->pcm_buf_size = PCM_BUF_MAX_SIZE_BYTES
			/ a2dp_codesize(&a2dpio->a2dp)
			* a2dp_codesize(&a2dpio->a2dp);
	iodev->buffer_size = (a2dpio->pcm_buf_size + block_size) / format_bytes;
	if (iodev->used_size > iodev->buffer_size)
		iodev->used_size = iodev->buffer_size;

	syslog(LOG_DEBUG, "a2dp iodev buf size %lu, used size %lu",
	       iodev->buffer_size, iodev->used_size);

	return 0;
}

static int close_dev(struct cras_iodev *iodev)
{
	int err;
	struct a2dp_io *a2dpio = (struct a2dp_io *)iodev;

	if (!a2dpio->transport)
		return 0;

	err = cras_bt_transport_release(a2dpio->transport);
	if (err < 0)
		syslog(LOG_ERR, "transport_release failed");

	a2dp_drain(&a2dpio->a2dp);
	a2dpio->pcm_buf_used = 0;
	cras_iodev_free_format(iodev);
	return 0;
}

static int is_open(const struct cras_iodev *iodev)
{
	struct a2dp_io *a2dpio = (struct a2dp_io *)iodev;
	return cras_bt_transport_fd(a2dpio->transport) > 0;
}

static int dev_running(const struct cras_iodev *iodev)
{
	// TODO(hychao): Track if a2dp iodev is actually playing.
	return is_open(iodev);
}

static int delay_frames(const struct cras_iodev *iodev)
{
	return frames_queued(iodev);
}

static int get_buffer(struct cras_iodev *iodev, uint8_t **dst, unsigned *frames)
{
	size_t format_bytes;
	struct a2dp_io *a2dpio;
	a2dpio = (struct a2dp_io *)iodev;

	format_bytes = cras_get_format_bytes(iodev->format);

	if (iodev->direction == CRAS_STREAM_OUTPUT) {
		*dst = a2dpio->pcm_buf + a2dpio->pcm_buf_offset
				+ a2dpio->pcm_buf_used;
		*frames = (a2dpio->pcm_buf_size - a2dpio->pcm_buf_offset -
				a2dpio->pcm_buf_used) / format_bytes;
	}
	return 0;
}

static int put_buffer(struct cras_iodev *iodev, unsigned nwritten)
{
	size_t format_bytes;
	int written = 0;
	int processed;
	struct a2dp_io *a2dpio = (struct a2dp_io *)iodev;

	format_bytes = cras_get_format_bytes(iodev->format);

	if (a2dpio->pcm_buf_used + a2dpio->pcm_buf_offset +
			nwritten * format_bytes > a2dpio->pcm_buf_size)
		return -1;
	a2dpio->pcm_buf_used += nwritten * format_bytes;

	processed = a2dp_write(a2dpio->pcm_buf + a2dpio->pcm_buf_offset,
			       a2dpio->pcm_buf_used,
			       &a2dpio->a2dp,
			       format_bytes,
			       cras_bt_transport_fd(a2dpio->transport),
			       cras_bt_transport_write_mtu(a2dpio->transport),
			       &written);

	if (a2dpio->pcm_buf_used == processed) {
		a2dpio->pcm_buf_offset = 0;
		a2dpio->pcm_buf_used = 0;
	} else {
		a2dpio->pcm_buf_offset += processed;
		a2dpio->pcm_buf_used -= processed;
	}

	if (written == -EAGAIN) {
		syslog(LOG_ERR, "a2dp_write error");
		return 0;
	}

	get_bt_queued_frames(iodev,
			     a2dp_block_size(&a2dpio->a2dp, written)
				     / format_bytes);
	return 0;
}

void free_resources(struct a2dp_io *a2dpio)
{
	free(a2dpio->base.supported_channel_counts);
	free(a2dpio->base.supported_rates);
	destroy_a2dp(&a2dpio->a2dp);
}

struct cras_iodev *a2dp_iodev_create(struct cras_bt_transport *transport)
{
	int err;
	struct a2dp_io *a2dpio;
	struct cras_iodev *iodev;
	struct cras_ionode *node;
	a2dp_sbc_t a2dp;

	a2dpio = (struct a2dp_io *)calloc(1, sizeof(*a2dpio));
	if (!a2dpio)
		goto error;

	a2dpio->transport = transport;
	cras_bt_transport_configuration(a2dpio->transport, &a2dp,
					sizeof(a2dp));
	err = init_a2dp(&a2dpio->a2dp, &a2dp);
	if (err) {
		syslog(LOG_ERR, "Fail to init a2dp");
		goto error;
	}

	iodev = &a2dpio->base;

	/* A2DP only does output now */
	iodev->direction = CRAS_STREAM_OUTPUT;

	snprintf(iodev->info.name, sizeof(iodev->info.name), "%s",
		 cras_bt_transport_object_path(a2dpio->transport));

	iodev->info.name[ARRAY_SIZE(iodev->info.name) - 1] = '\0';

	iodev->supported_rates =
			(size_t *)malloc(sizeof(*iodev->supported_rates));
	if (iodev->supported_rates == NULL)
		goto error;
	*iodev->supported_rates = 44100;

	iodev->supported_channel_counts =
			(size_t *)malloc(sizeof(
					*iodev->supported_channel_counts));
	if (iodev->supported_channel_counts == NULL)
		goto error;
	*iodev->supported_channel_counts = 2;

	iodev->open_dev = open_dev;
	iodev->is_open = is_open; /* Needed by thread_add_stream */
	iodev->frames_queued = frames_queued;
	iodev->dev_running = dev_running;
	iodev->delay_frames = delay_frames;
	iodev->get_buffer = get_buffer;
	iodev->put_buffer = put_buffer;
	iodev->close_dev = close_dev;
	iodev->update_supported_formats = update_supported_formats;

	/* Create a dummy ionode */
	node = (struct cras_ionode *)calloc(1, sizeof(*node));
	node->dev = iodev;
	strcpy(node->name, iodev->info.name);
	DL_APPEND(iodev->nodes, node);
	iodev->active_node = node;
	node->plugged = 1;
	node->priority = 3;
	gettimeofday(&node->plugged_time, NULL);

	/* A2DP does output only */
	err = cras_iodev_list_add_output(iodev);
	if (err)
		goto error;

	return iodev;
error:
	if (a2dpio) {
		free_resources(a2dpio);
		free(a2dpio);
	}
	return NULL;
}

void a2dp_iodev_destroy(struct cras_iodev *iodev)
{
	int rc;
	struct a2dp_io *a2dpio = (struct a2dp_io *)iodev;

	/* A2DP does output only */
	rc = cras_iodev_list_rm_output(iodev);
	if (rc != -EBUSY) {
		free_resources(a2dpio);
		free(a2dpio);
	}
}
